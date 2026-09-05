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

// Mycelium: see MyceliumNetwork.h / the matching GL3 shader for the full note.
uniform int hasMycelium;
uniform sampler2DRect myceliumSampler;
uniform vec2 myceliumGridOrigin;
uniform float myceliumGridStep;
uniform vec3 myceliumGlowColor;

// Vegetation: see VegetationField.h / the matching GL3 shader for the full note.
uniform int hasVegetation;
uniform sampler2DRect vegetationSampler;
uniform vec2 vegetationGridOrigin;
uniform float vegetationGridStep;

void main()
{
    float elevationNorm = clamp(depthfrag / heightMapNumEntries, 0.0, 1.0);

    vec4 color = vec4(1.0, 1.0, 1.0, 1.0);

    if (hasVegetation == 1)
    {
        vec2 vegUV = (texcoordfrag - vegetationGridOrigin) / vegetationGridStep;
        vec4 veg = texture2DRect(vegetationSampler, vegUV);

        if (veg.a > 0.75)
        {
            color.rgb = vec3(0.0, 0.0, elevationNorm);
        }
        else if (veg.a > 0.25)
        {
            color.rgb = vec3(1.0, 1.0, 1.0);
        }
        else if (veg.r > veg.g && veg.r > veg.b)
        {
            color.rgb = vec3(elevationNorm, 1.0, elevationNorm);
        }
        else if (veg.g > veg.r && veg.g > veg.b)
        {
            color.rgb = vec3(1.0, elevationNorm, elevationNorm);
        }
        else if (veg.b > veg.g && veg.b > veg.r)
        {
            color.rgb = vec3(0.0, elevationNorm, elevationNorm);
        }
        else
        {
            color.rgb = vec3(1.0, 0.6863, 0.6863);
        }
    }

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
