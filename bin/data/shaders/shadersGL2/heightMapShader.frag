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

--- Ecosystem fork: same organic-growth base color as shadersGL3's
version of this file - see that file's header note for the full
rationale. Kept in sync so behavior doesn't depend on which renderer
path ofIsGLProgrammableRenderer() picks at runtime.
***********************************************************************/

#version 120

varying float depthfrag;
varying vec2 texcoordfrag;

uniform sampler2DRect heightColorMapSampler;
uniform sampler2DRect pixelCornerElevationSampler; // Sampler for the half pixel texture
uniform float contourLineFactor;
uniform int drawContourLines;
uniform float heightMapNumEntries; // depthfrag is in [0, heightMapNumEntries) texel space, not 0..1
uniform float time;

// Mycelium: see MyceliumNetwork.h / the matching GL3 shader for the full note.
uniform int hasMycelium;
uniform sampler2DRect myceliumSampler;
uniform vec2 myceliumGridOrigin;
uniform float myceliumGridStep;
uniform vec3 myceliumGlowColor;

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

    float growth = fbm(texcoordfrag * 0.06);

    float pulse = 0.5 + 0.5 * sin(time * 0.4 + growth * 6.2831853);

    float growthThreshold = mix(0.8, 0.15, elevationNorm);
    float growthAmount = smoothstep(growthThreshold - 0.18, growthThreshold + 0.18, growth);
    growthAmount *= mix(0.8, 1.0, pulse);

    vec3 rockColor = mix(vec3(0.04, 0.04, 0.06), vec3(0.22, 0.19, 0.15), elevationNorm);
    vec3 lowGrowthColor = vec3(0.04, 0.30, 0.16);
    vec3 highGrowthColor = vec3(0.85, 0.95, 0.85);
    vec3 growthColor = mix(lowGrowthColor, highGrowthColor, elevationNorm);

    vec4 color = vec4(mix(rockColor, growthColor, growthAmount), 1.0);

    if (hasMycelium == 1)
    {
        vec2 myceliumUV = (texcoordfrag - myceliumGridOrigin) / myceliumGridStep;
        float myceliumIntensity = texture2DRect(myceliumSampler, myceliumUV).r;
        color.rgb += myceliumGlowColor * myceliumIntensity;
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
