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
  getCellColor()'s shrubs>fruits&&shrubs>nuts chain - no blending
  between types, ever. A tie (including the initial all-zero state,
  which is the common case: most of the play area starts, and often
  stays, unvegetated) renders as negative space rather than
  getCellColor()'s literal flat Color.PINK return value - see the
  no-op tie branch below for why: the reference photo of ELF's actual
  running sandbox shows bare land as plain lit sand, never a painted
  pink expanse, so PINK evidently never reaches the screen in practice.
- Each winning type's color is modulated by elevationNorm exactly as
  ELF's colors are modulated by cellheight (0..255 there, 0..1 here):
  shrub = (h, 1, h), fruit = (1, h, h), nut = (0, h, h).
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

// Vegetation: see VegetationField.h. vegetationSampler packs three plant
// densities (RGB) plus a water/snow flag (A) per grid cell;
// vegetationGridOrigin/Step convert texcoordfrag (kinect pixel space)
// into a texel lookup into it.
uniform int hasVegetation;
uniform sampler2DRect vegetationSampler;
uniform vec2 vegetationGridOrigin;
uniform float vegetationGridStep;

void main()
{
    // ELF's "cellheight" analog: a full-sensor-range normalized elevation
    // (0..1). In ELF, cellheight is the SAME field used both for
    // getCellColor()'s cosmetic modulation and for stepCells()'s
    // temperature/water/snow threshold comparisons - not two independent
    // normalizations. This mirrors that: elevationNorm here and
    // VegetationField::normalizedElevation() both normalize against the
    // same calibrated elevationMin/elevationMax range (see
    // VegetationField.h's header note and SandSurfaceRenderer's
    // getElevationMin()/getElevationMax()), just computed on different
    // sides (GPU here for display, CPU there for classification) since
    // they run at different times relative to each other.
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
        // Tie (including the initial all-zero state, which is by far the
        // common one - most of the play area starts and often stays
        // unvegetated): no branch above fires, so color is left exactly as
        // set before classification - the plain white "no augmentation"
        // base, i.e. genuine negative space. getCellColor() returns flat
        // Color.PINK on a tie in ELF's own source, but the reference photo
        // (ELFdev001/ELFDynamicSystem's actual sandbox output) shows bare
        // land as plain lit sand, never a painted pink expanse, and
        // vegetation itself reads as sparse scattered colored specks
        // rather than a solid area fill - ELF's renderer evidently only
        // marks pixels in proportion to density (so a 0-density cell never
        // actually receives that PINK draw call in practice), not a
        // per-cell flat fill the way this shader still does for the
        // non-tie cases above. Matching the photographed look here takes
        // priority over the literal, never-actually-visible return value.
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
