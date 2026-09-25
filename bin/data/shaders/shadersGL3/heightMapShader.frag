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

--- Ecosystem fork: no procedural rock/lichen texture. Water/snow/land
classification is still picked on the CPU side (VegetationField.h);
this shader turns that classification into color. Land's winner-take-
all comparison logic (strictly-dominant-channel, no blending between
types, matching BDlocation.getCellColor()'s shrubs>fruits&&shrubs>nuts
chain) is unchanged from the original ELF-literal version - see git
history/CLAUDE.md if the ELF-formula backstory is needed. The actual
COLOR VALUES below are a 2026-09-24 departure from ELF's literal ones
(and from this fork's own first-pass primary-color choices), per
explicit user request: a colorblind-accessible, visually cohesive
palette. Two different palettes, each used for what it's suited to:

1. SPECIES/AGENT COLORS (shrub/fruit/nut here; deer/hunter/gatherer/
   fisher in Critter.cpp/HumanAgent.cpp) keep each species' ORIGINAL
   hue family (shrub green, fruit red/warm, nut teal/cool - user's
   explicit instruction) but are deliberately NOT the muted Okabe-Ito
   reference values an earlier pass of this work used - confirmed on
   review that those changes were too conservative for several roles
   (gatherer/deer/fruit moved only ~17-23% of the max possible RGB
   distance from the originals - barely perceptible), because Okabe-Ito
   is tuned to fix ONLY the specific red-green collision (fruit peak
   and HUNTER_COLOR were the exact same (255,0,0), and fruit-vs-shrub's
   pure-red/pure-green pair is the classic worst case for red-green
   colorblindness, the most common form) without moving colors that
   were never part of that problem (yellow, cyan, blue) far from their
   familiar identity - a reasonable goal for data-viz, not the same
   goal as looking bold/distinct, which was also asked for. These
   values are bolder and more saturated instead, chosen to survive
   deuteranopia by the same underlying principle Okabe-Ito uses -
   separation via the RED and BLUE channels and overall lightness, not
   hue alone (deuteranopia collapses the green-sensing cone, so two
   colors differing only in "how green" still merge; they need to
   differ in red/blue intensity to stay apart) - while matching the
   jewel-toned saturation of the diverging ramp the user sourced
   themselves (part 2 below), for a visually cohesive whole rather than
   two mismatched palette styles. Each winning type still renders at
   FULL saturation with NO fade regardless of density (matching ELF's
   own no-threshold getCellColor() exactly, still) - only the peak
   color values changed, not the winner-take-all mechanic.
2. BACKGROUND/ELEVATION COLOR (water, and now negative-space/tie land
   too - see terrainRamp() below) uses a 7-stop diverging ramp the user
   sourced themselves (also confirmed colorblind-safe) - a sequential/
   diverging palette suits elevation specifically because elevation is
   physically continuous (adjacent cells have similar height), unlike
   species identity. Deliberately NOT applied to actively-growing
   vegetation: doing so would replace the winner-take-all speckled "CA"
   look (independent per-cell stochastic growth + no-fade coloring,
   confirmed on real hardware as looking correct) with a smooth
   gradient, since elevation doesn't have that per-cell independence -
   this was an explicit design question the user raised and this is
   the resolution: hybrid, not a full replacement.

Snow remains flat white, unmodulated (matching Color.WHITE), except in
debug mode (pale blue) - untouched by this palette work; it was never
part of the accessibility problem being fixed.

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
// Debug aid - see VegetationField::DEBUG_SHOW_SNOW's header note. Snow
// is normally flat white, identical to un-grown negative-space land;
// this tints it pale blue instead so the two are visually separable
// while diagnosing calibration.
uniform int debugShowSnow;
// New departure from ELF's literal getCellColor() (2026-09-24) - see
// VegetationField.h's NUT_VISIBILITY_THRESHOLD comment. Nut's color
// below, (0,h,h), has no channel pinned bright the way shrub/fruit do,
// so without this a single successful nut growth tick at low elevation
// renders solid near-black instantly - confirmed on real hardware as
// random black speckle scattered through otherwise-unrelated land, not
// a tie and not a contour line. Below this density, nut falls through
// to the same negative-space default as a real tie instead.
uniform float nutVisibilityThreshold;

// Background/elevation ramp - see the header note (part 2). The user's
// own 8-stop diverging palette (confirmed colorblind-safe; revised
// 2026-09-25 from an earlier 7-stop version specifically to drop the
// near-white middle stop that risked washing out against light-colored
// sand under a projector that can only add light, never subtract it -
// every stop here keeps real saturation), used for water and negative-
// space/tie land: the parts of the picture that are already purely
// elevation-driven and have no per-cell CA state to preserve.
// Piecewise-linear through all 8 stops rather than just picking two
// endpoints, so the full range - deep water up through unvegetated high
// ground - has real tonal variety instead of a flat wash. Values are the
// user-supplied hex codes converted to 0..1 float.
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

    // Pre-classification default - per explicit user choice (2026-09-24),
    // black rather than the previous full-brightness white: since a
    // projector can't project true black, this reads as "add no light,
    // let the sand's own tone show" rather than "cast a neutral white
    // wash." Overwritten below whenever vegetation classifies the cell as
    // something else. In practice this now only shows through briefly
    // before hasVegetation first becomes true (the grid isn't ready yet)
    // or with vegetation disabled entirely - NOT for ordinary untouched/
    // tie land during real operation, which renders via terrainRamp() in
    // the tie branch below instead (part of the 2026-09-24 palette work).
    vec4 color = vec4(0.0, 0.0, 0.0, 1.0);

    if (hasVegetation == 1)
    {
        vec2 vegUV = (texcoordfrag - vegetationGridOrigin) / vegetationGridStep;
        vec4 veg = texture(vegetationSampler, vegUV);

        if (veg.a > 0.75)
        {
            // Water - background ramp (part 2 of the header note), not a
            // species color. Water's actual elevationNorm range is narrow
            // (bounded above by the water line) so this mostly samples the
            // ramp's deep-blue end - still a real depth gradient, darker
            // in deeper water, same "gets darker with depth" convention
            // the ELF reference figure the user provided confirmed as
            // correct, just no longer literal (0,0,h) (which hit true
            // black at h=0, confusable with contour lines/void).
            color.rgb = terrainRamp(elevationNorm);
        }
        else if (veg.a > 0.25)
        {
            // Snow - BDlocation.getCellColor(): Color.WHITE, unmodulated -
            // except in debug mode, where it's tinted pale blue instead so
            // it doesn't read as identical to un-grown negative space.
            // Untouched by the 2026-09-24 palette work - see the header note.
            color.rgb = (debugShowSnow == 1) ? vec3(0.75, 0.85, 1.0) : vec3(1.0, 1.0, 1.0);
        }
        else if (veg.r > veg.g && veg.r > veg.b)
        {
            // Shrub-dominant - species color (part 1 of the header note),
            // a rich emerald, kept in shrub's original green family, R=0
            // so it never competes with fruit's high-red peak below.
            // Blends toward white as elevation rises, same pattern as
            // fruit/nut below - full saturation at low elevation, same
            // as ELF's own no-fade getCellColor().
            color.rgb = mix(vec3(0.0, 0.659, 0.349), vec3(1.0), elevationNorm);
        }
        else if (veg.g > veg.r && veg.g > veg.b)
        {
            // Fruit-dominant - species color, a bold orange-red, kept in
            // fruit's original red/warm family but with high red AND
            // enough green to sit clearly away from pure red - not the
            // same value as HUNTER_COLOR (see the header note), and
            // R=0.9/B=0.08 gives strong red-channel separation from
            // shrub's R=0 above, which is what actually survives
            // deuteranopia (hue alone wouldn't).
            color.rgb = mix(vec3(0.902, 0.353, 0.078), vec3(1.0), elevationNorm);
        }
        else if (veg.b > veg.g && veg.b > veg.r && veg.b > nutVisibilityThreshold)
        {
            // Nut-dominant - species color, a saturated teal, kept in
            // nut's original cool family, R=0/B high so it separates
            // from fruit via the same red/blue-channel logic. Formula
            // changed from the old black-at-h=0 pattern (0,h,h) to the
            // SAME peak-to-white pattern shrub/fruit use above - this
            // fixes the "random black speckle" problem at its root (see
            // CLAUDE.md's 2026-09-24 note): nut can no longer render
            // black at any elevation, so nutVisibilityThreshold below is
            // now a belt-and-suspenders single-tick-flash guard, not the
            // only fix.
            color.rgb = mix(vec3(0.0, 0.675, 0.675), vec3(1.0), elevationNorm);
        }
        else
        {
            // Tie (including the initial all-zero state, which is by far
            // the common one - most of the play area starts and often
            // stays unvegetated) OR nut below nutVisibilityThreshold:
            // background ramp (part 2 of the header note), same as water
            // above - untouched land now reads as a real hypsometric base
            // color by elevation rather than flat "no augmentation" white,
            // and vegetation still visibly claims a cell the instant it
            // establishes (full-saturation species color against this
            // more varied background, if anything MORE visible than
            // against flat white before).
            //
            // getCellColor() returns flat Color.PINK on a tie in ELF's own
            // source (confirmed by running ELF's own TESTING-mode
            // reference build) - this was already a deliberate departure
            // from that before this palette work, not a fidelity claim.
            color.rgb = terrainRamp(elevationNorm);
        }
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
