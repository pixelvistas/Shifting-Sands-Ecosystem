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

--- ss/terrain-reference-visuals branch (2026-10-19): reverted to the
literal pre-ecosystem-fork version of this file, byte-for-byte the same
color logic as it had before any ELF/vegetation work began (commit
`abb8fe3`'s parent - see CLAUDE.md's "Reverted to stock Magic Sand
height-colormap rendering" entry for the full rationale). This is also
exactly the mechanism Barthele, Sauer & Wilkening (2023) describe stock
SARndbox using: a height-interval color-key table ("HeightColorMap" -
see bin/data/colorMaps/HeightColorMap.xml, inherited unmodified from
the original Magic Sand base and never actually removed, just left
unused once the ecosystem fork's custom terrainRamp()/waterRamp()/
species-color logic took over as the primary color source) sampled
directly by elevation, no vegetation/species/water-depth classification
or overlay of any kind. `heightColorMapSampler` was already bound every
frame the whole time (SandSurfaceRenderer::drawSandbox() never stopped
setting it) - the ecosystem-fork shader just never sampled it. Reviving
it needed zero C++ changes, only this file.
***********************************************************************/

#version 150

out vec4 outputColor;

in float depthfrag;

uniform sampler2DRect heightColorMapSampler;
uniform sampler2DRect pixelCornerElevationSampler; // Sampler for the half pixel texture
uniform float contourLineFactor;
uniform int drawContourLines;
// Simulated water overlay - see main().
in vec2 texcoordfrag;
uniform sampler2DRect vegetationSampler;
uniform vec2 vegetationGridOrigin;
uniform float vegetationGridStep;
uniform int hasVegetation;
uniform int drawSimWater;
// The succession model's site layer (soil tint, M/S debug) - see main().
// Vegetation itself is not drawn on the sand (removed 2026-11-02).
uniform sampler2DRect siteSampler; // R = soil S, G = moisture M
uniform int hasSite;
uniform float soilTintStrength;
uniform int siteDebugView;          // 0 off, 1 moisture, 2 soil

// Operator debug false colour (dark purple - teal - yellow).
vec3 falseColour(float v)
{
    v = clamp(v, 0.0, 1.0);
    vec3 a = vec3(0.267, 0.005, 0.329), b = vec3(0.128, 0.567, 0.551), c = vec3(0.993, 0.906, 0.144);
    return v < 0.5 ? mix(a, b, v * 2.0) : mix(b, c, v * 2.0 - 1.0);
}


void main()
{
    vec2 depthPos = vec2(depthfrag, 0.5);
    vec4 color = texture2DRect(heightColorMapSampler, depthPos); // colormap converted depth

    vec2 vegUV = (texcoordfrag - vegetationGridOrigin) / vegetationGridStep;
    vec4 veg = (hasVegetation == 1) ? texture(vegetationSampler, vegUV) : vec4(0.0);
    float waterCode = veg.a * 255.0;
    bool simWater = waterCode > 0.5 && waterCode < 249.5;

    // Soil tint (succession model, 2026-10-28): a subtle brown in
    // proportion to soil S, wherever there is no simulated water.
    if (hasSite == 1 && !simWater)
    {
        float soilS = texture(siteSampler, vegUV).r;
        color.rgb = mix(color.rgb, vec3(0.42, 0.27, 0.13), soilTintStrength * soilS);
    }

    // Simulated water overlay (ss/terrain-reference-visuals, 2026-10-20).
    // The colour map above only shows elevation; this draws where
    // VegetationField's flow model actually has water. Its texture's
    // alpha packs water depth into 1..249 (0 = dry, 255 = snow, ignored
    // here), scaled by MAX_WATER_DEPTH_FRAC. Colours are the Deep/Shallow
    // Water stops from HeightColorMap.xml; deeper water is more opaque.
    // Water is drawn over a plain neutral lakebed (#B8B0A0, 2026-11-02),
    // not over the land colours, and these cells get no contour lines.
    bool drawWater = drawSimWater == 1 && simWater;
    if (drawWater)
    {
        color.rgb = vec3(0.722, 0.690, 0.627); // lakebed #B8B0A0
        float t = clamp((waterCode - 1.0) / 248.0, 0.0, 1.0);
        vec3 water = mix(vec3(0.255, 0.714, 0.769), vec3(0.031, 0.188, 0.420), t);
        color.rgb = mix(color.rgb, water, mix(0.6, 0.95, t));
    }

    // Operator debug overlay: moisture M or soil S in false colour.
    if (hasSite == 1 && siteDebugView > 0)
    {
        vec4 site = texture(siteSampler, vegUV);
        color.rgb = falseColour(siteDebugView == 1 ? site.g : site.r);
    }

    if (drawContourLines == 1 && !drawWater)
    {
        // Contour line computation
        /* Calculate the contour line interval containing each pixel corner by evaluating the half-pixel offset elevation texture: */
        float corner0=floor(texture2DRect(pixelCornerElevationSampler,vec2(gl_FragCoord.x,gl_FragCoord.y)).r*contourLineFactor);
        float corner1=floor(texture2DRect(pixelCornerElevationSampler,vec2(gl_FragCoord.x+1.0,gl_FragCoord.y)).r*contourLineFactor);
        float corner2=floor(texture2DRect(pixelCornerElevationSampler,vec2(gl_FragCoord.x,gl_FragCoord.y+1.0)).r*contourLineFactor);
        float corner3=floor(texture2DRect(pixelCornerElevationSampler,vec2(gl_FragCoord.x+1.0,gl_FragCoord.y+1.0)).r*contourLineFactor);

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
