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

--- Ecosystem fork: no procedural rock/lichen texture and no rainbow
height-ramp - neither ever appeared in any reference material (the ELF
photo, the ELF paper, the sound-sandbox/fluvial papers). Color here is a
direct GLSL port of BDlocation.getCellColor(): water/snow/land are still
picked on the CPU side (VegetationField.h), but this shader reproduces
ELF's exact color logic and formulas rather than an approximation of
them:

- Land: strictly-dominant-channel comparison, same as
  getCellColor()'s shrubs>fruits&&shrubs>nuts chain, with a tie
  (including the initial all-zero state) rendering as ELF's flat
  Color.PINK fallback - no blending between types, ever.
- Each winning type's color is modulated by elevationNorm exactly as
  ELF's colors are modulated by cellheight (0..255 there, 0..1 here):
  shrub = (h, 1, h), fruit = (1, h, h), nut = (0, h, h). PINK is NOT
  modulated, matching getCellColor() returning the flat Color.PINK
  constant on a tie.
- Water = (0, 0, h), also cellheight-modulated (a bathymetric gradient,
  darker in deeper water) - not the flat color this used to be.
- Snow = flat white, unmodulated, matching Color.WHITE exactly.

heightColorMapSampler is left bound but unused below, kept only so the
existing colormap-editing GUI machinery doesn't need to be torn out to
keep this compiling.
***********************************************************************/

#version 150

out vec4 outputColor;

in float depthfrag;
in vec2 texcoordfrag;

uniform sampler2DRect heightColorMapSampler;
uniform sampler2DRect pixelCornerElevationSampler; // Sampler for the half pixel texture
uniform float contourLineFactor;
uniform int drawContourLines;
uniform float heightMapNumEntries; // depthfrag is in [0, heightMapNumEntries) texel space, not 0..1 - see elevationNorm below
uniform float time; // unused - kept bound alongside heightColorMapSampler above

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

void main()
{
    // ELF's "cellheight" analog: a full-sensor-range normalized
    // elevation (0..1), independent of VegetationField's own
    // temperature-relative water/snow thresholds - exactly how ELF keeps
    // its MINDEPTH/MAXDEPTH-derived cellheight separate from
    // BDenvironment's LIVINGRANGE/temperature bands operating on it.
    float elevationNorm = clamp(depthfrag / heightMapNumEntries, 0.0, 1.0);

    // No colour cast before classification - full-brightness white, the
    // standard "no augmentation" convention for a projector. Overwritten
    // below whenever vegetation classifies the cell as something else;
    // only shows through with vegetation disabled.
    vec4 color = vec4(1.0, 1.0, 1.0, 1.0);

    if (hasVegetation == 1)
    {
        vec2 vegUV = (texcoordfrag - vegetationGridOrigin) / vegetationGridStep;
        vec4 veg = texture(vegetationSampler, vegUV);

        if (veg.a > 0.75)
        {
            // Water - BDlocation.getCellColor(): new Color(0, 0, cellheight).
            color.rgb = vec3(0.0, 0.0, elevationNorm);
        }
        else if (veg.a > 0.25)
        {
            // Snow - BDlocation.getCellColor(): Color.WHITE, unmodulated.
            color.rgb = vec3(1.0, 1.0, 1.0);
        }
        else if (veg.r > veg.g && veg.r > veg.b)
        {
            // Shrub-dominant - new Color(cellheight, 255, cellheight).
            color.rgb = vec3(elevationNorm, 1.0, elevationNorm);
        }
        else if (veg.g > veg.r && veg.g > veg.b)
        {
            // Fruit-dominant - new Color(255, cellheight, cellheight).
            color.rgb = vec3(1.0, elevationNorm, elevationNorm);
        }
        else if (veg.b > veg.g && veg.b > veg.r)
        {
            // Nut-dominant - new Color(0, cellheight, cellheight).
            color.rgb = vec3(0.0, elevationNorm, elevationNorm);
        }
        else
        {
            // Tie (including the initial all-zero state) - Color.PINK,
            // flat and unmodulated, matching getCellColor()'s final else.
            color.rgb = vec3(1.0, 0.6863, 0.6863);
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
