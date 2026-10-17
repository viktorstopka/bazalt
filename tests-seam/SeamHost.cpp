// The seam test host (wiki/plans/Release.md R0.5): the real plugin layer with
// no DAW and no WebView. It serves the built UI (ui/dist) over HTTP, answers the
// UI's native calls with the editor's own native functions, plays audio
// blocks in real time, and lets a test drive it all from headless Chromium
// (tests-seam/run.mjs).
//
//   SeamHost <ui/dist folder> [port]
//
// The page gets a small bridge script in place of JUCE's WebView glue:
//   window.__JUCE__.backend.emitEvent  ->  POST /__seam/post   (in order)
//   backend -> page events             <-  GET  /__seam/events (long poll)
// Test-only endpoints:
//   POST /__seam/note   {"note": 60, "velocity": 1, "on": true}
//   GET  /__seam/level  the output's peak over the last ~100 ms
//   POST /__seam/quit
// Known gap: the macro sliders' backend -> page updates need a real
// WebBrowserComponent and don't reach the page here.

#include "PluginEditor.h"
#include "PluginProcessor.h"
#include "bazalt/engine/patch/ExactFloat.h"

#include <juce_events/juce_events.h>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <mutex>
#include <csignal>
#include <thread>

namespace
{
    using bazalt::BazaltAudioProcessor;
    using bazalt::BazaltAudioProcessorEditor;

    /** Backend -> page events, collected for the page's long poll. */
    class EventQueue
    {
    public:
        void push (const juce::String& eventId, const juce::var& payload)
        {
            auto* event = new juce::DynamicObject();
            event->setProperty ("eventId", eventId);
            event->setProperty ("payload", payload);
            {
                const std::lock_guard lock (mutex);
                events.push_back (juce::var (event));
            }
            ready.notify_all();
        }

        /** A page (re)loaded: events now go to it. A reload leaves the old
            page's long poll open; it must not take the new page's events. */
        int newSession()
        {
            int session = 0;
            {
                const std::lock_guard lock (mutex);
                session = ++currentSession;
                events.clear();
            }
            ready.notify_all();
            return session;
        }

        /** Waits up to `timeoutMs` for at least one event, then takes them
            all — only for the current page's session. */
        juce::Array<juce::var> take (int session, int timeoutMs)
        {
            std::unique_lock lock (mutex);
            ready.wait_for (lock, std::chrono::milliseconds (timeoutMs),
                            [&] { return ! events.empty() || stopping || session != currentSession; });
            juce::Array<juce::var> out;
            if (session != currentSession)
                return out;
            for (auto& e : events)
                out.add (e);
            events.clear();
            return out;
        }

        void stop()
        {
            {
                const std::lock_guard lock (mutex);
                stopping = true;
            }
            ready.notify_all();
        }

    private:
        std::mutex mutex;
        std::condition_variable ready;
        std::deque<juce::var> events;
        bool stopping = false;
        int currentSession = 0;
    };

    /** Plays the processor in real time, ~10 ms blocks, on its own thread. */
    class AudioDriver : public juce::Thread
    {
    public:
        explicit AudioDriver (BazaltAudioProcessor& p) : juce::Thread ("seam audio"), processor (p) {}

        void addNote (int note, float velocity, bool on)
        {
            const std::lock_guard lock (midiMutex);
            pending.addEvent (on ? juce::MidiMessage::noteOn (1, note, velocity) : juce::MidiMessage::noteOff (1, note), 0);
        }

        float getRecentPeak() const noexcept { return recentPeak.load(); }

        void run() override
        {
            const auto channels = juce::jmax (processor.getTotalNumInputChannels(), processor.getTotalNumOutputChannels());
            juce::AudioBuffer<float> buffer (channels, blockSize);
            juce::MidiBuffer midi;
            auto next = juce::Time::getMillisecondCounterHiRes();
            float peak = 0.0f;
            int blocksInWindow = 0;
            while (! threadShouldExit())
            {
                buffer.clear();
                midi.clear();
                {
                    const std::lock_guard lock (midiMutex);
                    midi.swapWith (pending);
                }
                processor.processBlock (buffer, midi);
                for (int c = 0; c < processor.getTotalNumOutputChannels(); ++c)
                    peak = juce::jmax (peak, buffer.getMagnitude (c, 0, blockSize));
                if (++blocksInWindow >= 10) // ~100 ms
                {
                    recentPeak.store (peak);
                    peak = 0.0f;
                    blocksInWindow = 0;
                }
                next += 1000.0 * blockSize / sampleRate;
                const auto wait = next - juce::Time::getMillisecondCounterHiRes();
                if (wait > 0)
                    juce::Thread::sleep ((int) wait);
                else if (wait < -200.0)
                    next = juce::Time::getMillisecondCounterHiRes(); // fell behind: don't race to catch up
            }
        }

        static constexpr double sampleRate = 48000.0;
        static constexpr int blockSize = 480;

    private:
        BazaltAudioProcessor& processor;
        std::mutex midiMutex;
        juce::MidiBuffer pending;
        std::atomic<float> recentPeak { 0.0f };
    };

    struct Request
    {
        juce::String method, path, query;
        juce::MemoryBlock body;
    };

    bool readRequest (juce::StreamingSocket& socket, Request& request)
    {
        juce::MemoryBlock head;
        char c = 0;
        while (! head.toString().endsWith ("\r\n\r\n"))
        {
            if (socket.read (&c, 1, true) != 1 || head.getSize() > 65536)
                return false;
            head.append (&c, 1);
        }
        const auto lines = juce::StringArray::fromLines (head.toString());
        const auto first = juce::StringArray::fromTokens (lines[0], " ", "");
        request.method = first[0];
        request.path = juce::URL::removeEscapeChars (first[1].upToFirstOccurrenceOf ("?", false, false));
        request.query = first[1].fromFirstOccurrenceOf ("?", false, false);
        int length = 0;
        for (const auto& line : lines)
            if (line.startsWithIgnoreCase ("content-length:"))
                length = line.fromFirstOccurrenceOf (":", false, false).trim().getIntValue();
        request.body.setSize ((size_t) length);
        int got = 0;
        while (got < length)
        {
            const auto n = socket.read (static_cast<char*> (request.body.getData()) + got, length - got, true);
            if (n <= 0)
                return false;
            got += n;
        }
        return true;
    }

    void respond (juce::StreamingSocket& socket, int status, const juce::String& mime, const void* data, size_t size)
    {
        const auto reason = status == 200 ? "OK" : status == 204 ? "No Content" : "Not Found";
        const auto head = "HTTP/1.1 " + juce::String (status) + " " + reason + "\r\n"
                          + "Content-Type: " + mime + "\r\n"
                          + "Content-Length: " + juce::String ((juce::int64) size) + "\r\n"
                          + "Cache-Control: no-store\r\nConnection: close\r\n\r\n";
        socket.write (head.toRawUTF8(), (int) head.getNumBytesAsUTF8());
        if (size > 0)
            socket.write (data, (int) size);
    }

    void respondText (juce::StreamingSocket& socket, int status, const juce::String& mime, const juce::String& text)
    {
        respond (socket, status, mime, text.toRawUTF8(), text.getNumBytesAsUTF8());
    }

    juce::String mimeFor (const juce::File& file)
    {
        const auto ext = file.getFileExtension().toLowerCase();
        if (ext == ".html") return "text/html";
        if (ext == ".js" || ext == ".mjs") return "text/javascript";
        if (ext == ".css") return "text/css";
        if (ext == ".svg") return "image/svg+xml";
        if (ext == ".png") return "image/png";
        if (ext == ".json") return "application/json";
        if (ext == ".woff2") return "font/woff2";
        if (ext == ".wasm") return "application/wasm";
        return "application/octet-stream";
    }

    /** The page's stand-in for JUCE's WebView glue (juce_WebBrowserComponent.cpp's
        lowLevelIntegrationsScript), with the initialisation data inlined. */
    juce::String bridgeScript (const juce::WebBrowserComponent::Options& options)
    {
        std::map<juce::String, juce::Array<juce::var>> data;
        for (const auto& [key, value] : options.getInitialisationData())
            data[key].add (value);
        for (const auto& [name, function] : options.getNativeFunctions())
            data["__juce__functions"].add (name.toString());
        // Every key the interop library reads must exist. An empty platform
        // makes getBackendResourceAddress() return a same-origin path.
        for (const auto* key : { "__juce__platform", "__juce__functions", "__juce__registeredGlobalEventIds",
                                 "__juce__sliders", "__juce__toggles", "__juce__comboBoxes" })
            data[key];
        auto* init = new juce::DynamicObject();
        for (auto& [key, values] : data)
            init->setProperty (key, juce::var (values));

        return "(function () {\n"
               "  const init = " + bazalt::engine::toJson (juce::var (init)) + ";\n"
               R"js(
  const lists = new Map();
  let nextId = 0;
  const dispatch = (eventId, payload) => {
    const list = lists.get(eventId);
    if (list) for (const fn of [...list.values()]) fn(payload);
  };
  let chain = Promise.resolve();
  const backend = {
    addEventListener(eventId, fn) {
      if (!lists.has(eventId)) lists.set(eventId, new Map());
      const id = nextId++;
      lists.get(eventId).set(id, fn);
      return [eventId, id];
    },
    removeEventListener([eventId, id]) { lists.get(eventId)?.delete(id); },
    // In order, as JUCE's postMessage is.
    emitEvent(eventId, payload) {
      const body = JSON.stringify({ eventId, payload });
      chain = chain.then(() => fetch('/__seam/post', { method: 'POST', body }).catch(() => {}));
    },
    emitByBackend(eventId, json) { dispatch(eventId, JSON.parse(json)); },
  };
  window.__JUCE__ = { initialisationData: init, backend, postMessage() {} };
  window.__seamPending = () => chain;
  // Tests call native functions directly with this (negative ids never
  // collide with the interop library's own, which count up from 0).
  let seamId = -1;
  window.__seamInvoke = (name, ...params) => new Promise((resolve) => {
    const resultId = seamId--;
    const handle = backend.addEventListener('__juce__complete', (p) => {
      if (p.promiseId !== resultId) return;
      backend.removeEventListener(handle);
      resolve(p.result);
    });
    backend.emitEvent('__juce__invoke', { name, params, resultId });
  });
  (async function pump() {
    for (;;) {
      try {
        const r = await fetch('/__seam/events?s=__SEAM_SESSION__');
        for (const e of await r.json()) dispatch(e.eventId, e.payload);
      } catch {
        await new Promise((resolve) => setTimeout(resolve, 200));
      }
    }
  })();
})();
)js";
    }

    class SeamServer : public juce::Thread
    {
    public:
        SeamServer (BazaltAudioProcessor& p, juce::WebBrowserComponent::Options o, juce::File dist, AudioDriver& a)
            : juce::Thread ("seam http"), processor (p), options (std::move (o)), distRoot (std::move (dist)), audio (a),
              bridge (bridgeScript (options))
        {
        }

        bool listen (int port) { return socket.createListener (port, "127.0.0.1"); }

        void stopServing()
        {
            events.stop();
            signalThreadShouldExit();
            socket.close();
            stopThread (3000);
            while (activeConnections.load() > 0)
                juce::Thread::sleep (10);
        }

        void run() override
        {
            while (! threadShouldExit())
            {
                std::unique_ptr<juce::StreamingSocket> client (socket.waitForNextConnection());
                if (client == nullptr)
                    continue;
                ++activeConnections;
                std::thread ([this, c = std::move (client)]() mutable
                {
                    Request request;
                    if (readRequest (*c, request))
                        handle (*c, request);
                    c->close();
                    --activeConnections;
                }).detach();
            }
        }

    private:
        void handle (juce::StreamingSocket& client, const Request& request)
        {
            const auto& path = request.path;
            if (path == "/__seam/bridge.js")
                return respondText (client, 200, "text/javascript",
                                    bridge.replace ("__SEAM_SESSION__", juce::String (events.newSession())));
            if (path == "/__seam/post")
            {
                const auto message = juce::JSON::parse (request.body.toString());
                juce::MessageManager::callAsync ([this, message] { dispatchFromPage (message); });
                return respond (client, 204, "text/plain", nullptr, 0);
            }
            if (path == "/__seam/events")
                return respondText (client, 200, "application/json",
                                    bazalt::engine::toJson (juce::var (events.take (request.query.fromFirstOccurrenceOf ("s=", false, false).getIntValue(), 10000))));
            if (path == "/__seam/note")
            {
                const auto note = juce::JSON::parse (request.body.toString());
                audio.addNote ((int) note["note"], (float) (double) note.getProperty ("velocity", 1.0), (bool) note.getProperty ("on", true));
                return respond (client, 204, "text/plain", nullptr, 0);
            }
            if (path == "/__seam/level")
                return respondText (client, 200, "application/json", bazalt::engine::toJson (juce::var ((double) audio.getRecentPeak())));
            if (path == "/__seam/quit")
            {
                respond (client, 204, "text/plain", nullptr, 0);
                juce::MessageManager::callAsync ([] { juce::MessageManager::getInstance()->stopDispatchLoop(); });
                return;
            }

            if (path.startsWith ("/tap/") || path.startsWith ("/sample/"))
                if (auto provider = options.getResourceProvider())
                {
                    std::optional<juce::WebBrowserComponent::Resource> resource;
                    juce::WaitableEvent done;
                    juce::MessageManager::callAsync ([&] { resource = provider (path); done.signal(); });
                    done.wait (5000);
                    if (resource.has_value())
                        return respond (client, 200, resource->mimeType, resource->data.data(), resource->data.size());
                    return respond (client, 404, "text/plain", nullptr, 0);
                }

            const auto file = distRoot.getChildFile (path == "/" ? "index.html" : path.substring (1));
            if (! file.existsAsFile() || ! file.isAChildOf (distRoot))
                return respond (client, 404, "text/plain", nullptr, 0);
            if (file.getFileName() == "index.html")
            {
                auto html = file.loadFileAsString();
                html = html.replaceFirstOccurrenceOf ("<head>", "<head>\n<script src=\"/__seam/bridge.js\"></script>");
                return respondText (client, 200, "text/html", html);
            }
            juce::MemoryBlock bytes;
            file.loadFileAsData (bytes);
            respond (client, 200, mimeFor (file), bytes.getData(), bytes.getSize());
        }

        /** Message thread: what WebBrowserComponent does with a page's postMessage. */
        void dispatchFromPage (const juce::var& message)
        {
            const auto eventId = message["eventId"].toString();
            const auto& payload = message["payload"];
            if (eventId == "__juce__invoke")
            {
                const auto name = payload["name"].toString();
                const auto resultId = payload["resultId"];
                const auto& functions = options.getNativeFunctions();
                const auto it = functions.find (juce::Identifier (name));
                if (it == functions.end())
                {
                    std::fprintf (stderr, "seam: no native function '%s'\n", name.toRawUTF8());
                    return;
                }
                juce::Array<juce::var> params;
                if (auto* array = payload["params"].getArray())
                    params = *array;
                it->second (params, [this, resultId] (const juce::var& result)
                {
                    auto* complete = new juce::DynamicObject();
                    complete->setProperty ("promiseId", resultId);
                    complete->setProperty ("result", result);
                    events.push ("__juce__complete", juce::var (complete));
                });
                return;
            }
            for (const auto& [id, listener] : options.getEventListeners())
                if (id.toString() == eventId)
                    listener (payload);
        }

        BazaltAudioProcessor& processor;
        juce::WebBrowserComponent::Options options;
        juce::File distRoot;
        AudioDriver& audio;
        juce::String bridge;
        juce::StreamingSocket socket;
        EventQueue events;
        std::atomic<int> activeConnections { 0 };
    };
}

int main (int argc, char* argv[])
{
    if (argc < 2)
    {
        std::fprintf (stderr, "usage: SeamHost <ui/dist folder> [port]\n");
        return 2;
    }
    const juce::File dist (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    const auto port = argc > 2 ? juce::String (argv[2]).getIntValue() : 5180;
    if (! dist.getChildFile ("index.html").existsAsFile())
    {
        std::fprintf (stderr, "seam: %s has no index.html (run npm run build in ui/)\n", dist.getFullPathName().toRawUTF8());
        return 2;
    }

    juce::ScopedJuceInitialiser_GUI juce;
    auto processor = std::make_unique<BazaltAudioProcessor>();
    processor->prepareToPlay (AudioDriver::sampleRate, AudioDriver::blockSize);

    auto relays = BazaltAudioProcessorEditor::makeMacroRelays();
    AudioDriver audio (*processor);
    SeamServer server (*processor, BazaltAudioProcessorEditor::makeWebViewOptions (*processor, relays), dist, audio);
    if (! server.listen (port))
    {
        std::fprintf (stderr, "seam: can't listen on 127.0.0.1:%d\n", port);
        return 1;
    }
   #if ! JUCE_WINDOWS
    // The page closing a long poll mid-write is normal. Set after JUCE's own
    // start-up, which resets the handler.
    std::signal (SIGPIPE, SIG_IGN);
   #endif
    audio.startThread (juce::Thread::Priority::high);
    server.startThread();
    std::printf ("seam: serving %s on http://127.0.0.1:%d/\n", dist.getFullPathName().toRawUTF8(), port);
    std::fflush (stdout);

    juce::MessageManager::getInstance()->runDispatchLoop();

    server.stopServing();
    audio.stopThread (2000);
    processor->releaseResources();
    return 0;
}
