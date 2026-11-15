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
    uint OutlineContent;
    float GoodCutoff;
    float BadCutoff;
    uint ColorGood;
    uint ColorMed;
    uint ColorBad;
    uint ColorShield;
    uint OutlinePlacement;
    uint PulseEnabled;
    float AnimTime;
};

struct IconInstance
{
    uint position; // x in the low 16 bits, y in the high 16 bits, signed, whole pixels
    uint color;    // ARGB tint of the base icon
    uint layers01; // atlas slots of the underlay (low) and base icon (high); 0xFFFF = none
    uint layers23; // atlas slots of the paused (low) and stunned (high) overlays
    uint healthShield; // health ratio (low 16 bits, unorm16), shield ratio (bits 16..30, unorm15), bit 31: outline disabled
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
    nointerpolation float4 slotUv : TEXCOORD2;
    nointerpolation float2 texelStep : TEXCOORD3;
    nointerpolation uint layer : BLENDINDICES0;
    nointerpolation uint healthShield : BLENDINDICES1;
};

float4 UnpackArgb(uint argb)
{
    return float4((argb >> 16) & 0xFF, (argb >> 8) & 0xFF, argb & 0xFF, argb >> 24) / 255.0;
}

float SampleIconAlpha(float2 uv, float4 slotUv)
{
    if (uv.x < slotUv.x || uv.x > slotUv.x + slotUv.z || uv.y < slotUv.y || uv.y > slotUv.y + slotUv.w)
    {
        return 0.0;
    }
    return Atlas.SampleLevel(PointSampler, uv, 0).a;
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
        if (OutlineMode == 0 || baseSlot == 0xFFFF || (icon.healthShield & 0x80000000u) != 0)
        {
            output.position = float4(0.0, 0.0, 0.0, 1.0);
            output.color = float4(0.0, 0.0, 0.0, 0.0);
            output.uv = float2(0.0, 0.0);
            output.localPos = float2(0.0, 0.0);
            output.slotUv = float4(0.0, 0.0, 0.0, 0.0);
            output.texelStep = float2(0.0, 0.0);
            return output;
        }

        IconSlot slot = Slots[baseSlot];
        float2 pad = float2(4.0, 4.0);
        float2 halfSize = slot.halfSize + pad;
        float2 screen = float2(unitUv.x != 0.0 ? center.x + halfSize.x : center.x - halfSize.x,
                               unitUv.y != 0.0 ? center.y + halfSize.y : center.y - halfSize.y);

        output.position = mul(float4(screen, 0.0, 1.0), Composite);
        output.color = float4(1.0, 1.0, 1.0, 1.0);
        output.uv = slot.uv.xy + slot.uv.zw * (0.5 + (unitUv - 0.5) * (halfSize / slot.halfSize));
        output.localPos = (unitUv * 2.0 - 1.0) * halfSize;
        output.slotUv = slot.uv;
        output.texelStep = slot.uv.zw / (slot.halfSize * 2.0);
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
        output.slotUv = float4(0.0, 0.0, 0.0, 0.0);
        output.texelStep = float2(0.0, 0.0);
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
    output.slotUv = float4(0.0, 0.0, 0.0, 0.0);
    output.texelStep = float2(0.0, 0.0);
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

    // Layer 4: Procedural HP and Shield outline hugging icon silhouette
    if (OutlineMode == 0 || (input.healthShield & 0x80000000u) != 0)
    {
        discard;
    }

    float healthFrac = float(input.healthShield & 0xFFFF) / 65535.0;
    float shieldFrac = float((input.healthShield >> 16) & 0x7FFF) / 32767.0;

    // Mask layers based on OutlineContent: 0=Both, 1=HP only, 2=Shield only
    bool showHp = (OutlineContent != 2);
    bool showShield = (OutlineContent != 1);

    // Cutoff check: if cutoff enabled (SmartCutoff > 0.0), hide HP outline when healthy.
    // SmartCutoff does NOT apply to shield (shield outline is always drawn when active).
    bool hideHp = (!showHp || (SmartCutoff > 0.0 && healthFrac >= SmartCutoff));
    bool hasShield = (showShield && shieldFrac > 0.0);
    if (hideHp && !hasShield)
    {
        discard;
    }

    // Mask arcs based on OutlinePlacement: 0=Full, 1=Top, 2=Bottom, 3=Split (Shield Top, HP Bottom)
    bool allowHpPlacement = true;
    bool allowShieldPlacement = true;
    if (OutlinePlacement == 1)
    {
        allowHpPlacement = (input.localPos.y <= 0.0);
        allowShieldPlacement = (input.localPos.y <= 0.0);
    }
    else if (OutlinePlacement == 2)
    {
        allowHpPlacement = (input.localPos.y >= 0.0);
        allowShieldPlacement = (input.localPos.y >= 0.0);
    }
    else if (OutlinePlacement == 3)
    {
        allowShieldPlacement = (input.localPos.y <= 0.0);
        allowHpPlacement = (input.localPos.y >= 0.0);
    }

    if (!allowHpPlacement && !allowShieldPlacement)
    {
        discard;
    }

    // Pixels inside the base icon sprite are skipped (rendered by base layer 1)
    float centerAlpha = SampleIconAlpha(input.uv, input.slotUv);
    if (centerAlpha > 0.3)
    {
        discard;
    }

    float2 step = input.texelStep;

    // Inner band (radius 1 texel): HP outline directly hugging the icon silhouette
    float aN  = SampleIconAlpha(input.uv + float2( 0.0, -1.0) * step, input.slotUv);
    float aS  = SampleIconAlpha(input.uv + float2( 0.0,  1.0) * step, input.slotUv);
    float aW  = SampleIconAlpha(input.uv + float2(-1.0,  0.0) * step, input.slotUv);
    float aE  = SampleIconAlpha(input.uv + float2( 1.0,  0.0) * step, input.slotUv);
    float aNW = SampleIconAlpha(input.uv + float2(-1.0, -1.0) * step, input.slotUv);
    float aNE = SampleIconAlpha(input.uv + float2( 1.0, -1.0) * step, input.slotUv);
    float aSW = SampleIconAlpha(input.uv + float2(-1.0,  1.0) * step, input.slotUv);
    float aSE = SampleIconAlpha(input.uv + float2( 1.0,  1.0) * step, input.slotUv);
    float maxA1 = max(max(max(aN, aS), max(aW, aE)), max(max(aNW, aNE), max(aSW, aSE)));

    // Outer band (radius 2 texels): Shield outline
    float maxA2 = 0.0;
    if (hasShield && allowShieldPlacement)
    {
        float aN2  = SampleIconAlpha(input.uv + float2( 0.0, -2.0) * step, input.slotUv);
        float aS2  = SampleIconAlpha(input.uv + float2( 0.0,  2.0) * step, input.slotUv);
        float aW2  = SampleIconAlpha(input.uv + float2(-2.0,  0.0) * step, input.slotUv);
        float aE2  = SampleIconAlpha(input.uv + float2( 2.0,  0.0) * step, input.slotUv);
        float aNW2 = SampleIconAlpha(input.uv + float2(-1.5, -1.5) * step, input.slotUv);
        float aNE2 = SampleIconAlpha(input.uv + float2( 1.5, -1.5) * step, input.slotUv);
        float aSW2 = SampleIconAlpha(input.uv + float2(-1.5,  1.5) * step, input.slotUv);
        float aSE2 = SampleIconAlpha(input.uv + float2( 1.5,  1.5) * step, input.slotUv);
        maxA2 = max(max(max(aN2, aS2), max(aW2, aE2)), max(max(aNW2, aNE2), max(aSW2, aSE2)));
    }

    if (maxA1 <= 0.3 && maxA2 <= 0.3)
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

    // HP outline determination
    bool isHp = (maxA1 > 0.3) && allowHpPlacement && showHp;
    float hpAlpha = 0.0;
    if (isHp && !hideHp)
    {
        if (OutlineMode == 1)
        {
            hpAlpha = 1.0;
        }
        else if (OutlineMode == 2)
        {
            hpAlpha = (healthFrac > 0.0 && normAngle <= healthFrac) ? 1.0 : 0.0;
        }

        // Low-HP pulse modulation
        if (PulseEnabled != 0 && healthFrac <= BadCutoff && healthFrac > 0.0)
        {
            hpAlpha *= (0.35 + 0.65 * pow(sin(AnimTime * 9.0) * 0.5 + 0.5, 1.8));
        }
    }

    // HP color smoothly interpolated from ColorBad to ColorMed to ColorGood
    float4 cBad = UnpackArgb(ColorBad);
    float4 cMed = UnpackArgb(ColorMed);
    float4 cGood = UnpackArgb(ColorGood);
    float denom = max(GoodCutoff - BadCutoff, 0.001);
    float t = saturate((healthFrac - BadCutoff) / denom);
    float4 hpColor = t < 0.5 ? lerp(cBad, cMed, t * 2.0) : lerp(cMed, cGood, (t - 0.5) * 2.0);

    // Shield outline determination (band 2, outside HP band)
    float shieldAlpha = 0.0;
    float4 shieldColor = UnpackArgb(ColorShield);
    if (!isHp && maxA2 > 0.3 && hasShield && allowShieldPlacement)
    {
        float shieldAngleAlpha = (shieldFrac > 0.0 && normAngle <= shieldFrac) ? 1.0 : 0.0;
        shieldAlpha = shieldAngleAlpha;
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
