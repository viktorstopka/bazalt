// World-space -> NDC transform (screen = world*zoom + pan, same convention
// as InfiniteCanvas.tsx's Camera, then screen pixels -> clip space,
// flipping Y for screen-down vs. NDC-up) shared by every shader here.
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

// Cable line shader (M10, node editor): the world->clip transform above,
// plus a per-vertex "distance travelled along this cable so far" (a_dist,
// in the same px units as the position attribute) and a per-vertex dash
// flag (a_dash: 0 or 1, constant across one tessellated cable so
// interpolation between its own vertices never produces a fractional
// in-between value). One draw call renders solid and dashed cables
// together — the fragment shader only discards where a_dash>0.5, per
// ADR-0010's rejected/hover wire-feedback styling.
export const cableLineVertexShader = `${transformVertexPrelude}
in vec2 a_position;
in vec4 a_color;
in float a_dist;
in float a_dash;
out vec4 v_color;
out float v_dist;
out float v_dash;

void main() {
  gl_Position = vec4(worldToClip(a_position), 0.0, 1.0);
  v_color = a_color;
  v_dist = a_dist;
  v_dash = a_dash;
}
`

export const cableLineFragmentShader = `#version 300 es
precision mediump float;
in vec4 v_color;
in float v_dist;
in float v_dash;
uniform float u_dashPeriod;
out vec4 outColor;

void main() {
  if (v_dash > 0.5 && mod(v_dist, u_dashPeriod) > u_dashPeriod * 0.5) discard;
  outColor = v_color;
}
`
