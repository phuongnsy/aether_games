$input v_texcoord0, v_color0

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);  // scene color

uniform vec4 u_chroma;  // x=amount

void main()
{
	vec2 d = v_texcoord0 - vec2(0.5, 0.5);
	vec2 off = d * u_chroma.x;
	float r = texture2D(s_texColor, v_texcoord0 + off).r;
	float g = texture2D(s_texColor, v_texcoord0).g;
	float b = texture2D(s_texColor, v_texcoord0 - off).b;
	float a = texture2D(s_texColor, v_texcoord0).a;
	gl_FragColor = vec4(r, g, b, a);
}
