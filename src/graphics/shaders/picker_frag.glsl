#version 450

layout(location = 4) in flat uint vInstanceID;

layout(location = 0) out uint outObjectID;

layout(push_constant) uniform Picker {
    uint mouseX;
    uint mouseY;
    uint instanceOffset;
} p;

void main() {

    ivec2 fragCoord = ivec2(gl_FragCoord.xy);
    
    if( fragCoord.x != p.mouseX || fragCoord.y != p.mouseY ) {
        discard;
    }
    
    outObjectID = vInstanceID + p.instanceOffset;
    
}