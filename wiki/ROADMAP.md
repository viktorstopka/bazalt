# Roadmap

The user's own roadmap (2026-10-08), recorded verbatim below. The plans that turn each
stage into buildable batches live in `wiki/plans/`:

| Stage | Plan |
|---|---|
| 0. User experience polish | small, built directly (no plan file) |
| 1. Data manipulation and wavetable synthesis | `plans/DataAndWavetable.md` |
| 2. Stochastic-natural and life-cycle suite | `plans/InstanceAxis.md` (the engine half, direction agreed); a research report comes before the rest |
| 3. Sampling and audio data suite | — |
| 4. Physical suite | `plans/BakedPhysics.md`, `plans/SpatialScene.md` (earlier proposals) |
| 5. Standard DSP effects suite | — |

Decisions taken while planning, 2026-10-08: Listen is never saved with the patch; the
effect build is a second build of the same code (working name **Bazalt Erosion**, not
final); the Convolver moves from stage 4 to stage 3 (an impulse response is audio data);
stage 2 starts with a research report on stochastic and natural processes before any
plan.

---

### 0. USER EXPERIENCE POLISH 🖱️

Ctrl + dragging from a node (do not collide with clicking sockets), creates a line with a + symbol from that point to current mouse cursor. When mouse cursor hover over a different Add-compatible node, line goes green. When releasing, those 2 nodes get added using Add node.

Ctrl+D when selected a node duplicates it and attaches to your mouse similar to when placing. Ctrl+X, Ctrl+C, Ctrl+V works for nodes. Ctrl+R renames it (now it refreshes the whole UI). Ctrl+A selects all nodes. Ctrl+Y adds a Remap node from node main mod output.

R spawns a Map node. A spawns an Add node. S and M spawns a Multiply node.

Ctrl+Alt Click on Audio Output Socket adds Listen. Listen overrides Master Out. Adding a Listen deletes every other Listen already present. Cltr+Alt Click on a port, that already has a Listen removes the Listen. Double clicking a Listen removes Listen.

### 1. DATA MANIPULATION AND WAVETABLE SYNTHESIS 🤖

These are nodes are systems designed to make everything possible. Turn anything into anything using nodes and auxiliary tools called Factories. Use Wavetable editor and data manipulation to create and transform any waveform with natural and existing sounds inspiring useful ways to transform waveforms modulary. Use Curve editor to create any LFO-style curves and use EQ Curve to carve any sound. With EQ, a new View node is implemented, called “Spectrum”, that takes advantage of the EQ design and applies it for a read-only view node. Data types, data transformation, an other manipulation is now perfectly polished to a state, where anything is doable and connectable, allowing for crazy experimentation, but mainly effective workflow to create controls that the user needs. We are not thinking in the bounds of modern audio tools, but rather in a way that will make later stages achievable, without cluttering the software. This is one of the last opportunities to think destructively regarding the architecture to achieve a cleaner result, so we need to think about: things that are unnecessary (such as To Mod/To Audio, From Bool adapters, but even more complex things and functionalities of the software) or work against a perfect system and are implemented rather for the estabilished practices, than it being the best way. It also solves redundancy that clutters the project and makes the workflow harder to learn - common problem here is for example a duplicate for control and audio - Multiply/Gain are similar (which is confusing, when Mix has already been removed), other instances (maybe Clip/Ceil). Also, new nodes are added to fix gaps in data manipulation, if any. Together with this step, a very simple README.md is now added to the GitHub project, with a link to a public NODE_GUIDE.md, where every single node and its prop is described. Part of this sweep is a great cleanup in the Nodes categories and names. Nodes are categorized better so they match NOT estabilished audio terms that come from the analog era, but rather smart, descriptive relevant names. The whole plugin is Nature-inspired and so names should follow this, but only for things that are actually inspired by nature in the development or the nature name makes perfect sense. Never by force or for terms that make sense as is. This means Allocation could now take name of Life-cycle, but Curve should stay Curve. LFO is a bad name, but also not a useful node and should be removed immediatelly. For Oscillator, it should adapt the look of Sine, Saw or Square (removing Pitch+Fine+PulseW props). Shape prop turns into the new Factory-supported prop, that accepts a curve. Wavetable is now achieved by plugging a simple Curve output from a Wavetable node into Oscillator and automating the Frame value in the Wavetable node. What you would use LFO for in other synths, Oscillator can do as well, as it has the proper properties. ADSR should be rethought in a way, where it can also be controlled by a curve and edited in a Curve editor by adding points (A, D, S, H, R,…) to the curve. Moving these will then scale the curve appropriately. This should be the first step as it creates an environment for the other changes to adapt better names.

### 2. STOCHASTIC-NATURAL AND LIFE-CYCLE SUITE 🦎

These nodes introduce nature-based complex systems of randoms, chaoses and physical properties. They will also introduce the concept of genotypes to introduce even more complex and nature-based data manipulation. Allocators are now handled perfectly and in a way, where template Allocators cover any case. The whole system is now rethought in a way, where it allows for big amounts of instances and introduces architecture layer, that allows for better instance management. The low number Voice+Voice Sum,… limits that exist now are analysied and removed not in a simple remove-a-limit way - it is there likely for a reason. But rather rethink it so the system allows for 100 instances, when it is not a heavy chain. A nice test that things work propely could be the possibility to create a granular synthesis patch without a granular synthesis node after 3. Sampling and Audio Data Suite, via these life-cycle nodes.

### 3. SAMPLING AND AUDIO DATA SUITE 🎙️

These nodes handle audio sampling, whether simple such as playing a sample, or complexsuch as granulizer tools and other new revolutionary tools. You can now use “Audio In” nodeto create effects that don’t use the MIDI layer. The VST is now possible to use as both effectand instrument with the proper soft UI distinction (warnings,...) and hard limits. The emphasishere is to create a layer that works with audio, but still matches the modularity of the plugin.We are not implementing audio by simple using the already estabilished audio templates andprocessing, but rethinking it in a way that is matches the philosophy. We also think aboutusing audio files and types as a way to store other data such as a control sample or later abaked physics sample. This could be analogical to using textures for BDSR in 3D graphics,even splitting an image into R, G, B and using a different channel for different physical textures. With audio getting a proper treatment, we get new appropriate View nodes - most notably - a Spectrogram (a Spectrum style view is already implemented with EQ factory).

### 4. PHYSICAL SUITE ☄️

These nodes introduce integration of sound and 3D physical space concepts such as rigid body or other simulations. All of this while keeping in mind the broader vision of a 3D editor that allows for sound. First demos will only include ways to introduce 3D via nodes, such as distributions for particle-system-like allocations, very simple rigid body, friction or resonating a real 3D mesh. This should also include a Convolver for instance.

### 5. STANDARD DSP EFFECTS SUITE 📇

Here, the rest of standard effects that haven’t yet been implemented are finally added. It is the last phase, because we want to avoid following standard audio practices that are not useful to us. Therefore in this stage, we will know which effects we actually need, how exactly should they look and other things. Most of these effects are not crucial.
