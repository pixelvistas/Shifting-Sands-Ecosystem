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
literal pre-ecosystem-fork version of this file - see the matching GL3
shader's header note for the full rationale (stock Magic Sand's
height-colormap mechanism, the same one Barthele et al. describe
SARndbox using, never actually removed from this codebase - just
unused once the ecosystem fork's custom color logic took over).
***********************************************************************/

#version 120

varying float depthfrag;

uniform sampler2DRect heightColorMapSampler;
uniform sampler2DRect pixelCornerElevationSampler; // Sampler for the half pixel texture
uniform float contourLineFactor;
uniform int drawContourLines;
// Simulated water overlay - see main().
varying vec2 texcoordfrag;
uniform sampler2DRect vegetationSampler;
uniform vec2 vegetationGridOrigin;
uniform float vegetationGridStep;
uniform int hasVegetation;
uniform int drawSimWater;

void main()
{
    vec2 depthPos = vec2(depthfrag, 0.5);
    vec4 color = texture2DRect(heightColorMapSampler, depthPos); // colormap converted depth

    // Simulated water overlay (ss/terrain-reference-visuals, 2026-10-20).
    // The colour map above only shows elevation; this draws where
    // VegetationField's flow model actually has water. Its texture's
    // alpha packs water depth into 1..249 (0 = dry, 255 = snow, ignored
    // here), scaled by MAX_WATER_DEPTH_FRAC. Colours are the Deep/Shallow
    // Water stops from HeightColorMap.xml; deeper water is more opaque.
    if (drawSimWater == 1 && hasVegetation == 1)
    {
        vec2 vegUV = (texcoordfrag - vegetationGridOrigin) / vegetationGridStep;
        float a = texture2DRect(vegetationSampler, vegUV).a * 255.0;
        if (a > 0.5 && a < 249.5)
        {
            float t = clamp((a - 1.0) / 248.0, 0.0, 1.0);
            vec3 water = mix(vec3(0.255, 0.714, 0.769), vec3(0.031, 0.188, 0.420), t);
            color.rgb = mix(color.rgb, water, mix(0.6, 0.95, t));
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
