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

--- Ecosystem fork: same ELF-accurate color logic as shadersGL3's version
of this file - see that file's header note for the full rationale. Kept
in sync so behavior doesn't depend on which renderer path
ofIsGLProgrammableRenderer() picks at runtime.
***********************************************************************/

#version 120

varying float depthfrag;
varying vec2 texcoordfrag;

uniform sampler2DRect heightColorMapSampler;
uniform sampler2DRect pixelCornerElevationSampler; // Sampler for the half pixel texture
uniform float contourLineFactor;
uniform int drawContourLines;
uniform float heightMapNumEntries; // depthfrag is in [0, heightMapNumEntries) texel space, not 0..1 - see elevationNorm below
uniform float time; // unused - kept bound alongside heightColorMapSampler above

// Vegetation: see VegetationField.h / the matching GL3 shader for the full note.
uniform int hasVegetation;
uniform sampler2DRect vegetationSampler;
uniform vec2 vegetationGridOrigin;
uniform float vegetationGridStep;
uniform int debugShowSnow; // see the matching GL3 shader's header note
uniform float nutVisibilityThreshold; // see the matching GL3 shader's header note
uniform float waterLevelFrac; // see the matching GL3 shader's header note

// See the matching GL3 shader's waterRamp() for the full rationale.
vec3 waterRamp(float t)
{
    vec3 deep = vec3(0.000, 0.259, 0.616);    // #00429d
    vec3 shallow = vec3(0.294, 0.451, 0.635); // #4b73a2
    return mix(deep, shallow, clamp(t, 0.0, 1.0));
}

// See the matching GL3 shader's terrainRamp() for the full rationale.
// 8-stop version, revised 2026-09-25 to drop the near-white middle stop.
vec3 terrainRamp(float t)
{
    vec3 c0 = vec3(0.000, 0.259, 0.616); // #00429d
    vec3 c1 = vec3(0.294, 0.451, 0.635); // #4b73a2
    vec3 c2 = vec3(0.494, 0.639, 0.690); // #7ea3b0
    vec3 c3 = vec3(0.729, 0.824, 0.773); // #bad2c5
    vec3 c4 = vec3(0.847, 0.792, 0.749); // #d8cabf
    vec3 c5 = vec3(0.737, 0.580, 0.596); // #bc9498
    vec3 c6 = vec3(0.651, 0.357, 0.420); // #a65b6b
    vec3 c7 = vec3(0.576, 0.000, 0.227); // #93003a

    t = clamp(t, 0.0, 1.0) * 7.0;
    if (t < 1.0) return mix(c0, c1, t);
    if (t < 2.0) return mix(c1, c2, t - 1.0);
    if (t < 3.0) return mix(c2, c3, t - 2.0);
    if (t < 4.0) return mix(c3, c4, t - 3.0);
    if (t < 5.0) return mix(c4, c5, t - 4.0);
    if (t < 6.0) return mix(c5, c6, t - 5.0);
    return mix(c6, c7, t - 6.0);
}

void main()
{
    float elevationNorm = clamp(depthfrag / heightMapNumEntries, 0.0, 1.0);

    // Pre-classification default - black, see the matching GL3 shader's
    // note (2026-09-24 user choice; only shows through pre-startup/with
    // vegetation disabled, not for ordinary tie land - see terrainRamp()).
    vec4 color = vec4(0.0, 0.0, 0.0, 1.0);

    if (hasVegetation == 1)
    {
        vec2 vegUV = (texcoordfrag - vegetationGridOrigin) / vegetationGridStep;
        vec4 veg = texture2DRect(vegetationSampler, vegUV);

        if (veg.a > 0.75)
        {
            // Water - dedicated waterRamp(), renormalized to water's own
            // reachable depth range - see the matching GL3 shader's note.
            float waterDepthT = clamp(elevationNorm / max(waterLevelFrac, 0.0001), 0.0, 1.0);
            color.rgb = waterRamp(waterDepthT);
        }
        else if (veg.a > 0.25)
        {
            color.rgb = (debugShowSnow == 1) ? vec3(0.75, 0.85, 1.0) : vec3(1.0, 1.0, 1.0);
        }
        else if (veg.r > veg.g && veg.r > veg.b)
        {
            // Shrub-dominant - bold emerald, see GL3 note.
            color.rgb = mix(vec3(0.0, 0.659, 0.349), vec3(1.0), elevationNorm);
        }
        else if (veg.g > veg.r && veg.g > veg.b)
        {
            // Fruit-dominant - bold orange-red, see GL3 note.
            color.rgb = mix(vec3(0.902, 0.353, 0.078), vec3(1.0), elevationNorm);
        }
        else if (veg.b > veg.g && veg.b > veg.r && veg.b > nutVisibilityThreshold)
        {
            // Nut-dominant - saturated teal, peak-to-white pattern (no
            // longer black-at-low-elevation) - see GL3 note.
            color.rgb = mix(vec3(0.0, 0.675, 0.675), vec3(1.0), elevationNorm);
        }
        else
        {
            // Tie OR nut below nutVisibilityThreshold - background ramp,
            // see the matching GL3 shader's note.
            color.rgb = terrainRamp(elevationNorm);
        }
    }

    if (drawContourLines == 1)
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

    gl_FragColor = color;
}
