@vs vs
in vec4 pos;
in vec2 texcoord0;
out vec2 uv;

void main() {
    gl_Position = pos;
    uv = texcoord0;
}
@end

@fs fs
uniform sampler2D tex;

in vec2 uv;
out vec4 frag_color;

void main() {
    frag_color = texture(tex, uv);
}
@end

@program texture vs fs


