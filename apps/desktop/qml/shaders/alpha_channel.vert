// Unshaded custom materials get no UV varying by default: pass UV0 to the fragment stage.
VARYING vec2 texcoord;

void MAIN()
{
    texcoord = UV0;
    POSITION = MODELVIEWPROJECTION_MATRIX * vec4(VERTEX, 1.0);
}
