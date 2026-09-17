// Shared world-space -> NDC transform for every primitive kind this spike
// draws (dots, lines, node rectangles): screen = world*zoom + pan (same
// convention as ui/src/canvas/InfiniteCanvas.tsx's Camera), then screen
// pixels -> clip space, flipping Y (screen-down vs. NDC-up).
const transformVertexPrelude = `#version 300 es
uniform vec2 u_pan;
uniform float u_zoom;
uniform vec2 u_resolution;

vec2 worldToClip(vec2 worldPos) {
  vec2 screenPos = worldPos * u_zoom + u_pan;
  vec2 ndc = (screenPos / u_resolution) * 2.0 - 1.0;
  return vec2(ndc.x, -ndc.y);
}
`

export const dotVertexShader = `${transformVertexPrelude}
in vec2 a_position;
uniform float u_pointSize;

void main() {
  gl_Position = vec4(worldToClip(a_position), 0.0, 1.0);
  gl_PointSize = u_pointSize;
}
`

export const solidFragmentShader = `#version 300 es
precision mediump float;
uniform vec4 u_color;
out vec4 outColor;

void main() {
  outColor = u_color;
}
`

export const lineVertexShader = `${transformVertexPrelude}
in vec2 a_position;
in vec4 a_color;
out vec4 v_color;

void main() {
  gl_Position = vec4(worldToClip(a_position), 0.0, 1.0);
  v_color = a_color;
}
`

export const varyingColorFragmentShader = `#version 300 es
precision mediump float;
in vec4 v_color;
out vec4 outColor;

void main() {
  outColor = v_color;
}
`
