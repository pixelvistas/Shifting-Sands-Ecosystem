/***********************************************************************
heightMapShader - Shader fragment to display color and contourlines.
Copyright (c) 2016 Thomas Wolf

-- adapted from SurfaceAddContourLines by Oliver Kreylos
Copyright (c) 2012 Oliver Kreylos

This file is part of the Magic Sand.

The Magic Sand is free software; you can redistribute it
and/or modify it under the terms of the GNU General Public License as
published by the Free Software Foundation; either version 2 of the
License, or (at your option) any later version.

The Magic Sand is distributed in the hope that it will be
useful, but WITHOUT ANY WARRANTY; without even the implied warranty of
MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
General Public License for more details.

You should have received a copy of the GNU General Public License along
with the Magic Sand; if not, write to the Free Software
Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA 02111-1307 USA

--- Ecosystem fork: base color is now a procedural organic-growth look
(informed by Ji & Wakefield's "Inhabitat", Leonardo 51:4, 2018 - lichen-
like biomass that thickens and brightens toward ridgelines/peaks, dark
and sparse in valleys) instead of the original 1D rainbow height-ramp
lookup, since that ramp is the single most recognizable "generic AR
sandbox" visual signature. heightColorMapSampler is left bound but
unused below, kept only so the existing colormap-editing GUI machinery
doesn't need to be torn out to keep this compiling.

--- Vegetation layer: on top of that base, VegetationField.h's ELF-style
flora grid is blended in as flat-colored patches (water/snow/three plant
types), the same cellular, patchy-blob look documented in Murgatroyd et
al.'s ELF AR sandbox paper and matched against a real photo of it - see
vegetationSampler below and the blend logic near the end of main().
***********************************************************************/

#version 150

out vec4 outputColor;

in float depthfrag;
in vec2 texcoordfrag;

uniform sampler2DRect heightColorMapSampler;
uniform sampler2DRect pixelCornerElevationSampler; // Sampler for the half pixel texture
uniform float contourLineFactor;
uniform int drawContourLines;
uniform float heightMapNumEntries; // depthfrag is in [0, heightMapNumEntries) texel space, not 0..1
uniform float time;

// Mycelium: see MyceliumNetwork.h. myceliumSampler is a coarse grid
// texture (networkPattern x revealedAccum); myceliumGridOrigin/Step
// convert texcoordfrag (kinect pixel space) into a texel lookup into it.
uniform int hasMycelium;
uniform sampler2DRect myceliumSampler;
uniform vec2 myceliumGridOrigin;
uniform float myceliumGridStep;
uniform vec3 myceliumGlowColor;

// Vegetation: see VegetationField.h. vegetationSampler packs three plant
// densities (RGB) plus a water/snow flag (A) per grid cell;
// vegetationGridOrigin/Step convert texcoordfrag (kinect pixel space)
// into a texel lookup into it, same convention as the mycelium sampler.
uniform int hasVegetation;
uniform sampler2DRect vegetationSampler;
uniform vec2 vegetationGridOrigin;
uniform float vegetationGridStep;

float hash(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

float valueNoise(vec2 p)
{
    vec2 i = floor(p);
    vec2 f = fract(p);
    float a = hash(i);
    float b = hash(i + vec2(1.0, 0.0));
    float c = hash(i + vec2(0.0, 1.0));
    float d = hash(i + vec2(1.0, 1.0));
    vec2 u = f * f * (3.0 - 2.0 * f);
    return mix(a, b, u.x) + (c - a) * u.y * (1.0 - u.x) + (d - b) * u.x * u.y;
}

float fbm(vec2 p)
{
    float value = 0.0;
    float amplitude = 0.5;
    for (int i = 0; i < 4; i++)
    {
        value += amplitude * valueNoise(p);
        p *= 2.0;
        amplitude *= 0.5;
    }
    return value;
}

void main()
{
    float elevationNorm = clamp(depthfrag / heightMapNumEntries, 0.0, 1.0);

    // texcoordfrag is the kinect depth-image pixel coordinate (see the
    // vertex shader), so this pattern stays locked to the physical sand
    // rather than drifting with the screen/projector.
    float growth = fbm(texcoordfrag * 0.06);

    // Subtle breathing rather than a hard on/off flicker - "pulsating
    // while it grows," per the reference this look is modeled on.
    float pulse = 0.5 + 0.5 * sin(time * 0.4 + growth * 6.2831853);

    // Growth patches are rarer and dimmer in low valleys, denser and
    // brighter at ridgelines/peaks - elevation sets the *tendency*, noise
    // sets the blotchy, irregular patch shapes, so it reads as organic
    // biomass coverage rather than a smooth height-banded gradient.
    float growthThreshold = mix(0.8, 0.15, elevationNorm);
    float growthAmount = smoothstep(growthThreshold - 0.18, growthThreshold + 0.18, growth);
    growthAmount *= mix(0.8, 1.0, pulse);

    vec3 rockColor = mix(vec3(0.04, 0.04, 0.06), vec3(0.22, 0.19, 0.15), elevationNorm);
    vec3 lowGrowthColor = vec3(0.04, 0.30, 0.16);
    vec3 highGrowthColor = vec3(0.85, 0.95, 0.85);
    vec3 growthColor = mix(lowGrowthColor, highGrowthColor, elevationNorm);

    vec4 color = vec4(mix(rockColor, growthColor, growthAmount), 1.0);

    if (hasVegetation == 1)
    {
        vec2 vegUV = (texcoordfrag - vegetationGridOrigin) / vegetationGridStep;
        vec4 veg = texture(vegetationSampler, vegUV);

        // A touch of per-pixel jitter on the patch edges so cell
        // boundaries read as a frayed, organic edge instead of a hard
        // pixel grid - the blocky, speckled look of the real ELF sandbox
        // comes from exactly this kind of noise at cell boundaries.
        float edgeJitter = (hash(texcoordfrag * 3.7) - 0.5) * 0.35;

        if (veg.a > 0.75)
        {
            // Water - flat dark blue-black, per BDlocation::getCellColor.
            color.rgb = mix(color.rgb, vec3(0.0, 0.04, 0.16), 0.9);
        }
        else if (veg.a > 0.25)
        {
            // Snow - flat white.
            color.rgb = mix(color.rgb, vec3(0.96, 0.97, 1.0), 0.9);
        }
        else
        {
            // Bright green / reddish-pink / dark teal, matching the three
            // predominant-plant-type colors described for the ELF Dynamic
            // System (shrub/fruit/nut in BDlocation.getCellColor terms).
            vec3 shrubColor = vec3(0.25, 0.85, 0.35);
            vec3 fruitColor = vec3(0.95, 0.35, 0.55);
            vec3 nutColor   = vec3(0.15, 0.55, 0.60);

            float total = veg.r + veg.g + veg.b;
            vec3 patchColor = (veg.r * shrubColor + veg.g * fruitColor + veg.b * nutColor) / max(total, 0.0001);
            float coverage = clamp(total + edgeJitter, 0.0, 1.0);
            color.rgb = mix(color.rgb, patchColor, coverage);
        }
    }

    if (hasMycelium == 1)
    {
        vec2 myceliumUV = (texcoordfrag - myceliumGridOrigin) / myceliumGridStep;
        float myceliumIntensity = texture(myceliumSampler, myceliumUV).r;
        color.rgb += myceliumGlowColor * myceliumIntensity;
    }

    if (drawContourLines == 1)
    {
        // Contour line computation
        /* Calculate the contour line interval containing each pixel corner by evaluating the half-pixel offset elevation texture: */
        float corner0=floor(texture(pixelCornerElevationSampler,vec2(gl_FragCoord.x,gl_FragCoord.y)).r*contourLineFactor);
        float corner1=floor(texture(pixelCornerElevationSampler,vec2(gl_FragCoord.x+1.0,gl_FragCoord.y)).r*contourLineFactor);
        float corner2=floor(texture(pixelCornerElevationSampler,vec2(gl_FragCoord.x,gl_FragCoord.y+1.0)).r*contourLineFactor);
        float corner3=floor(texture(pixelCornerElevationSampler,vec2(gl_FragCoord.x+1.0,gl_FragCoord.y+1.0)).r*contourLineFactor);

        /* Find all pixel edges that cross at least one contour line: */
        int edgeMask=0;
        int numEdges=0;
        if(corner0!=corner1)
        {
            edgeMask+=1;
            ++numEdges;
        }
        if(corner2!=corner3)
        {
            edgeMask+=2;
            ++numEdges;
        }
        if(corner0!=corner2)
        {
            edgeMask+=4;
            ++numEdges;
        }
        if(corner1!=corner3)
        {
            edgeMask+=8;
            ++numEdges;
        }

        /* Check for all cases in which the pixel should be colored as a topographic contour line: */
        if(numEdges>2||edgeMask==3||edgeMask==12||(numEdges==2&&mod(floor(gl_FragCoord.x)+floor(gl_FragCoord.y),2.0)==0.0))
        {
            /* Topographic contour lines are rendered in black: */
            color=vec4(0.0,0.0,0.0,1.0);
        }
    }

    outputColor = color;
}
