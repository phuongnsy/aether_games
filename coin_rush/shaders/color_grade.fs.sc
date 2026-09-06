$input v_texcoord0, v_color0

#include <bgfx_shader.sh>

SAMPLER2D(s_texColor, 0);  // scene color

uniform vec4 u_grade;  // x=brightness, y=contrast, z=saturation

void main()
{
	vec4 c = texture2D(s_texColor, v_texcoord0);
	vec3 col = c.rgb * u_grade.x;
	col = (col - 0.5) * u_grade.y + 0.5;
	float l = dot(col, vec3(0.299, 0.587, 0.114));
	col = mix(vec3(l, l, l), col, u_grade.z);
	gl_FragColor = vec4(clamp(col, 0.0, 1.0), c.a);
}
