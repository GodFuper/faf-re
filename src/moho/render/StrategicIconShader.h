#pragma once

// Engine-owned HLSL for the GPU-driven strategic icons (StrategicIconRenderer).
//
// One instance per icon, 30 vertices per instance: up to five screen quads
// stacked in the order the CPU path draws them (identified underlay, base
// icon, paused overlay, stunned overlay, and procedural outline). The vertex
// shader pulls the icon record and the atlas slot of each layer from structured
// buffers and builds the same corners, UVs and colours `CD3DPrimBatcher` builds
// on the CPU for `DrawQuad` (TStrategicIcon). Layer 4 is the procedural health
// and shield outline. A layer without a texture (or disabled outline) collapses
// to a zero-area quad. The pixel shader reproduces the TStrategicIcon pass for
// layers 0..3: point sample, and replace the texel's colour with the vertex
// colour where the texel is close to mid grey; alpha from the texture; blended
// src-alpha/inv-src-alpha into RGB only, no depth. Layer 4 procedurally generates
// an antialiased inner HP arc and outer shield ring.
//
// Bindings follow gpg/gal/GpuDriven.h (b0, t0..t3 buffers, t4 texture, s0),
// so DXC builds the same source for D3D12 (DXIL) and Vulkan (SPIR-V).

namespace moho
{
  inline constexpr char kStrategicIconShaderSource[] = R"hlsl(
cbuffer StrategicIconConstants : register(b0)
{
    row_major float4x4 Composite; // CD3DPrimBatcher's view * projection
    uint OutlineMode;
    float SmartCutoff;
    uint ShowShield;
    float GoodCutoff;
    float BadCutoff;
    uint ColorGood;
    uint ColorMed;
    uint ColorBad;
    uint ColorShield;
    float3 _Pad;
};

struct IconInstance
{
    uint position; // x in the low 16 bits, y in the high 16 bits, signed, whole pixels
    uint color;    // ARGB tint of the base icon
    uint layers01; // atlas slots of the underlay (low) and base icon (high); 0xFFFF = none
    uint layers23; // atlas slots of the paused (low) and stunned (high) overlays
    uint healthShield; // health ratio (low 16 bits, unorm), shield ratio (high 16 bits, unorm)
};

struct IconSlot
{
    float4 uv;       // atlas origin (xy) and extent (zw), as CD3DPrimBatcher::SetTexture keeps them
    float2 halfSize; // the texture's width >> 1 and height >> 1
    float2 unused;
};

StructuredBuffer<IconInstance> Instances : register(t0);
StructuredBuffer<IconSlot> Slots : register(t1);
Texture2D Atlas : register(t4);
SamplerState PointSampler : register(s0);

struct IconVertex
{
    float4 position : SV_Position;
    float4 color : COLOR0;
    float2 uv : TEXCOORD0;
    float2 localPos : TEXCOORD1;
    nointerpolation uint layer : BLENDINDICES0;
    nointerpolation uint healthShield : BLENDINDICES1;
};

float4 UnpackArgb(uint argb)
{
    return float4((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF, argb >> 24) / 255.0;
}

IconVertex StrategicIconVS(uint vertexId : SV_VertexID, uint instanceId : SV_InstanceID)
{
    // DrawQuad's two triangles: corners 0,1,2 and 2,3,0 (top left, top right,
    // bottom right, bottom left).
    static const uint kCornerOfVertex[6] = { 0, 1, 2, 2, 3, 0 };

    IconInstance icon = Instances[instanceId];
    uint layer = vertexId / 6;
    uint corner = kCornerOfVertex[vertexId % 6];
    float2 unitUv = float2(corner == 1 || corner == 2 ? 1.0 : 0.0, corner >= 2 ? 1.0 : 0.0);
    float2 center = float2((int)(icon.position << 16) >> 16, (int)icon.position >> 16);

    IconVertex output;
    output.layer = layer;
    output.healthShield = icon.healthShield;

    if (layer == 4)
    {
        uint baseSlot = (icon.layers01 >> 16) & 0xFFFF;
        if (OutlineMode == 0 || baseSlot == 0xFFFF)
        {
            output.position = float4(0.0, 0.0, 0.0, 1.0);
            output.color = float4(0.0, 0.0, 0.0, 0.0);
            output.uv = float2(0.0, 0.0);
            output.localPos = float2(0.0, 0.0);
            return output;
        }

        IconSlot slot = Slots[baseSlot];
        float2 halfSize = slot.halfSize * 1.45;
        float2 screen = float2(unitUv.x != 0.0 ? center.x + halfSize.x : center.x - halfSize.x,
                               unitUv.y != 0.0 ? center.y + halfSize.y : center.y - halfSize.y);

        output.position = mul(float4(screen, 0.0, 1.0), Composite);
        output.color = float4(1.0, 1.0, 1.0, 1.0);
        output.uv = float2(0.0, 0.0);
        output.localPos = unitUv * 2.0 - 1.0;
        return output;
    }

    uint packed = layer < 2 ? icon.layers01 : icon.layers23;
    uint slotIndex = (packed >> ((layer & 1) * 16)) & 0xFFFF;

    if (slotIndex == 0xFFFF)
    {
        output.position = float4(0.0, 0.0, 0.0, 1.0);
        output.color = float4(0.0, 0.0, 0.0, 0.0);
        output.uv = float2(0.0, 0.0);
        output.localPos = float2(0.0, 0.0);
        return output;
    }

    IconSlot slot = Slots[slotIndex];
    float2 halfSize = slot.halfSize;
    float2 screen = float2(unitUv.x != 0.0 ? center.x + halfSize.x : center.x - halfSize.x,
                           unitUv.y != 0.0 ? center.y + halfSize.y : center.y - halfSize.y);

    output.position = mul(float4(screen, 0.0, 1.0), Composite);
    // Only the base icon (layer 1) is tinted; the others draw at 0xFFFFFFFF.
    output.color = UnpackArgb(layer == 1 ? icon.color : 0xFFFFFFFF);
    output.uv = slot.uv.xy + slot.uv.zw * unitUv;
    output.localPos = unitUv * 2.0 - 1.0;
    return output;
}

float4 StrategicIconPS(IconVertex input) : SV_Target
{
    if (input.layer < 4)
    {
        float4 color = Atlas.Sample(PointSampler, input.uv);
        float3 d = (color.rgb - float3(0.5, 0.5, 0.5)) * (color.rgb - float3(0.5, 0.5, 0.5));
        if (dot(d, float3(1.0, 1.0, 1.0)) < 0.25)
        {
            color.rgb = input.color.rgb;
        }
        return color;
    }

    // Layer 4: Procedural HP and Shield outline
    if (OutlineMode == 0)
    {
        discard;
    }

    float healthFrac = float(input.healthShield & 0xFFFF) / 65535.0;
    float shieldFrac = float((input.healthShield >> 16) & 0xFFFF) / 65535.0;

    // Smart mode check: hide outline if healthy and shield is not damaged
    if (OutlineMode == 3 && healthFrac >= SmartCutoff && (shieldFrac >= 1.0 || shieldFrac <= 0.0))
    {
        discard;
    }

    float r = length(input.localPos);
    if (r > 1.05 || r < 0.55)
    {
        discard;
    }

    // Angle starting at 12 o'clock (0.0) sweeping clockwise to 1.0
    static const float kTwoPi = 6.28318530718;
    float angle = atan2(input.localPos.x, -input.localPos.y);
    if (angle < 0.0)
    {
        angle += kTwoPi;
    }
    float normAngle = angle / kTwoPi;

    float dr = max(fwidth(r) * 1.5, 0.005);

    // Inner band (r in [0.60, 0.85]): HP outline
    float hpRadial = smoothstep(0.60 - dr, 0.60 + dr, r) * (1.0 - smoothstep(0.85 - dr, 0.85 + dr, r));
    float hpAngleAlpha = 0.0;
    if (OutlineMode == 1)
    {
        hpAngleAlpha = 1.0;
    }
    else if (OutlineMode == 2 || OutlineMode == 3)
    {
        if (OutlineMode == 3 && healthFrac >= SmartCutoff)
        {
            hpAngleAlpha = 0.0;
        }
        else
        {
            hpAngleAlpha = (healthFrac > 0.0 && normAngle <= healthFrac) ? 1.0 : 0.0;
        }
    }
    float hpAlpha = hpRadial * hpAngleAlpha;

    // HP color smoothly interpolated from ColorBad to ColorMed to ColorGood
    float4 cBad = UnpackArgb(ColorBad);
    float4 cMed = UnpackArgb(ColorMed);
    float4 cGood = UnpackArgb(ColorGood);
    float denom = max(GoodCutoff - BadCutoff, 0.001);
    float t = saturate((healthFrac - BadCutoff) / denom);
    float4 hpColor = t < 0.5 ? lerp(cBad, cMed, t * 2.0) : lerp(cMed, cGood, (t - 0.5) * 2.0);

    // Outer band (r in [0.85, 1.00]): Shield outline
    float shieldAlpha = 0.0;
    float4 shieldColor = UnpackArgb(ColorShield);
    if (ShowShield != 0 && shieldFrac > 0.0)
    {
        float shieldRadial = smoothstep(0.85 - dr, 0.85 + dr, r) * (1.0 - smoothstep(1.00 - dr, 1.00 + dr, r));
        float shieldAngleAlpha = (shieldFrac > 0.0 && normAngle <= shieldFrac) ? 1.0 : 0.0;
        shieldAlpha = shieldRadial * shieldAngleAlpha;
    }

    float totalAlpha = saturate(hpAlpha + shieldAlpha);
    if (totalAlpha < 0.01)
    {
        discard;
    }

    float3 rgb = (hpColor.rgb * hpAlpha + shieldColor.rgb * shieldAlpha) / max(hpAlpha + shieldAlpha, 0.0001);
    return float4(rgb, totalAlpha);
}
)hlsl";
} // namespace moho
