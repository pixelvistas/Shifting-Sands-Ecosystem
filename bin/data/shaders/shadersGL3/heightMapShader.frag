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

--- Ecosystem fork: base color is a flat, neutral "bare sand" tone - no
procedural rock/lichen texture. That noise-driven look (and the rainbow
height-ramp before it) never appeared in any reference material (the ELF
photo, the ELF paper, the sound-sandbox/fluvial papers): ELF's own
renderer is flat categorical color, nothing else, and this now matches
that directly. heightColorMapSampler is left bound but unused below,
kept only so the existing colormap-editing GUI machinery doesn't need to
be torn out to keep this compiling.

--- Vegetation layer: VegetationField.h's ELF-style flora grid is blended
over that flat base as flat-colored patches (water/snow/three plant
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
uniform float heightMapNumEntries; // depthfrag is in [0, heightMapNumEntries) texel space, not 0..1 - unused now that the base color no longer varies with elevation, left bound alongside heightColorMapSampler above
uniform float time; // unused now that the base color no longer pulses, same reasoning

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

// Still used below for the vegetation patch edge jitter (see hasVegetation).
float hash(vec2 p)
{
    return fract(sin(dot(p, vec2(127.1, 311.7))) * 43758.5453123);
}

void main()
{
    // Flat bare-sand color - what shows through before any vegetation has
    // grown on a cell, or permanently on any part of the mesh with
    // vegetation disabled. Deliberately plain: ELF's own "nothing growing
    // here yet" cells are just flat color too.
    vec4 color = vec4(0.88, 0.85, 0.76, 1.0);

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
