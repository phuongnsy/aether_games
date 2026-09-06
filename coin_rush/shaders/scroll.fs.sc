$input v_texcoord0, v_color0

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);  // base texture

uniform vec4 u_scroll;  // xy=accumulated uv offset

void main()
{
	vec2 uv = v_texcoord0 + u_scroll.xy;
	gl_FragColor = texture2D(s_texColor, uv) * v_color0;
}
