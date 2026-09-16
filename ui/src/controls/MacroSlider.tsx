import { useEffect, useReducer } from 'react'
import { getSliderState } from '@juce-framework/webview'
import './MacroSlider.css'

interface MacroSliderProps {
  /** Must match the name a WebSliderRelay was constructed with in
      PluginEditor.cpp (e.g. "oscShape") — this is how the two sides find
      each other; there's no discovery mechanism beyond matching strings.
  */
  relayName: string
  label: string
}

/** Temporary control (MILESTONES.md M5: "osc shape, filter cutoff/
    resonance, envelope... bound through the full round trip"). Round-trips
    through JUCE's built-in WebSliderRelay/WebSliderParameterAttachment
    mechanism (PluginEditor.cpp), not the M7+ command bridge — this is a
    plain host-automatable AudioProcessorParameter, which already has a
    first-party JUCE mechanism for exactly this. Host automation moving the
    parameter fires `valueChangedEvent`, which this component listens for,
    satisfying the "host automation reflected back to the UI" exit
    criterion.
*/
export function MacroSlider({ relayName, label }: MacroSliderProps) {
  // getSliderState() is idempotent (cached by name on the JS side), so
  // calling it every render always returns the same SliderState instance —
  // no ref/memo needed, and re-renders come from the listener below.
  const state = getSliderState(relayName)
  const [, forceUpdate] = useReducer((n: number) => n + 1, 0)

  useEffect(() => {
    const valueListenerId = state.valueChangedEvent.addListener(forceUpdate)
    const propsListenerId = state.propertiesChangedEvent.addListener(forceUpdate)
    return () => {
      state.valueChangedEvent.removeListener(valueListenerId)
      state.propertiesChangedEvent.removeListener(propsListenerId)
    }
  }, [state])

  const normalized = state.getNormalisedValue()
  const scaled = state.getScaledValue()

  return (
    <div className="macro-slider">
      <div className="macro-slider-header">
        <span className="macro-slider-label">{label}</span>
        <span className="macro-slider-value">{scaled.toFixed(2)}</span>
      </div>
      <input
        type="range"
        className="macro-slider-input"
        min={0}
        max={1}
        step={0.0001}
        value={normalized}
        onPointerDown={() => state.sliderDragStarted()}
        onPointerUp={() => state.sliderDragEnded()}
        onChange={(e) => state.setNormalisedValue(Number(e.target.value))}
      />
    </div>
  )
}
