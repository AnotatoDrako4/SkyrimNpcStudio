// Diagnostic view: the diffuse alpha channel as grey (white = opaque, black = fully transparent).
// Parts without a diffuse texture show white, as the game treats them as opaque.
VARYING vec2 texcoord;

void MAIN()
{
    float a = hasMap > 0.5 ? texture(diffuseMap, texcoord).a : 1.0;
    FRAGCOLOR = vec4(a, a, a, 1.0);
}
