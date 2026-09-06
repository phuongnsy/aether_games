$input v_texcoord0, v_color0

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);  // base texture

uniform vec4 u_glow;  // x=intensity, y=pulse [0..1]

void main()
{
	vec4 c = texture2D(s_texColor, v_texcoord0) * v_color0;
	c.rgb *= 1.0 + u_glow.x * u_glow.y;
	gl_FragColor = c;
}
