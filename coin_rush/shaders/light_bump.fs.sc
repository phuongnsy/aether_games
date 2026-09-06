$input v_texcoord0, v_color0

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);  // normal G-buffer: rgb=world normal, a=occluder

uniform vec4 u_light;  // x=height, y=normal strength, z=falloff power

void main()
{
	vec2 q = v_texcoord0 - vec2(0.5, 0.5);           // quad-local, [-0.5, 0.5]
	float rr = clamp(1.0 - length(q) * 2.0, 0.0, 1.0);
	float atten = pow(rr, max(u_light.z, 0.001));    // radial falloff
	vec2 suv = gl_FragCoord.xy * u_viewTexel.xy;      // this fragment, screen uv
	vec2 nxy = (texture2D(s_texColor, suv).xy * 2.0 - 1.0) * u_light.y;
	vec3 N = normalize(vec3(nxy, 1.0));               // reconstruct +Z world normal
	// dir fragment->light: uv.y is texture-space (down), world +Y up -> flip y.
	vec3 L = normalize(vec3(-q.x, q.y, u_light.x));   // u_light.x = light height
	float ndl = max(dot(N, L), 0.0);
	gl_FragColor = vec4(v_color0.rgb * atten * ndl, 1.0);
}
