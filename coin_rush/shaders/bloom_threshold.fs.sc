$input v_texcoord0, v_color0

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);  // scene color

uniform vec4 u_bloom;  // x=threshold, y=knee

void main()
{
	vec4 c = texture2D(s_texColor, v_texcoord0);
	float l = dot(c.rgb, vec3(0.299, 0.587, 0.114));
	float t = smoothstep(u_bloom.x, u_bloom.x + max(u_bloom.y, 0.001), l);
	gl_FragColor = vec4(c.rgb * t, 1.0);
}
