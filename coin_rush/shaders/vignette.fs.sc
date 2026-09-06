$input v_texcoord0, v_color0

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);  // scene color

uniform vec4 u_vignette;  // x=start radius, y=softness, z=intensity

void main()
{
	vec4 c = texture2D(s_texColor, v_texcoord0);
	vec2 d = v_texcoord0 - vec2(0.5, 0.5);
	float r = length(d) * 1.41421356;
	float v = smoothstep(u_vignette.x, u_vignette.x + max(u_vignette.y, 0.001), r);
	c.rgb *= 1.0 - v * u_vignette.z;
	gl_FragColor = c;
}
