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
// The succession model's site layer (soil tint, M/S debug) - see main().
// Vegetation itself is not drawn on the sand (removed 2026-11-02).
uniform sampler2DRect siteSampler; // R = soil S, G = moisture M
uniform int hasSite;
uniform float soilTintStrength;
uniform int siteDebugView;          // 0 off, 1 moisture, 2 soil
// Individual plants (PlantLayer, 2026-11-03): one RGBA8 state texel per
// plant site - R = role + 1 (0 none), G = radius / plantRadiusScale,
// B/A = the site's position within its grid cell. Drawn in mm on the sand.
uniform sampler2DRect plantSampler;
uniform int hasPlants;
uniform vec2 plantSites;          // sites in x and y
uniform float plantSpacingMM;
uniform float plantMmPerCell;
uniform float plantRadiusScale;   // mm
// Water lines (2026-11-05): glowing bathymetric contours on simulated
// water only, from the same per-pixel elevation as the colour map
// (depthfrag). waterLineTransform maps depthfrag to elevation / spacing.
uniform int drawWaterLines;
uniform vec2 waterLineTransform;
uniform float waterLineGlowPx;     // halo falloff, projector pixels
uniform float waterLineBrightness; // 0..1
uniform int drawShorelineGlow;
// Glowing land lines (2026-11-06): contour lines on non-water pixels, each
// in the colour of the band at its own level, same core + halo as the
// water lines. h = colour-map key height in mm = depthfrag * x + y.
uniform int drawLandLines;
uniform int landLineMode;            // 0 band edges, 1 every N mm
uniform vec2 heightTransform;
uniform float landLineSpacing;       // mm (every-N mode)
uniform float landLineOffset;        // mm (every-N mode)
uniform int landEdgeCount;           // band edges in use (<= 16)
uniform float landEdge[16];          // band edges of the colour map, mm
uniform float landEdgeGap[16];       // mm to the nearest other edge
uniform float landLineGlowPx;
uniform float landLineBrightness;
uniform float dimUnderPlants;        // 0..1
uniform int bandFill;                // 1 colour-map fill, 0 land starts black
// Flat-ground fade (water and land lines): slope fitted to 8 samples of the
// filtered depth (the texture the vertex shader reads) at +-5 Kinect pixels
// along both axes and both diagonals, so per-pixel Kinect noise does not
// read as slope (fwidth alone cannot tell noise from slope). 0 = off.
uniform float flatFadeSlope;         // tan of the threshold angle
uniform sampler2DRect tex0;          // filtered depth, bound for the vertex shader
uniform mat4 kinectWorldMatrix;
uniform vec2 depthTransformation;
uniform vec4 basePlaneEq;
uniform vec4 kinectROIRect;          // first and last mesh vertex (x0, y0, x1, y1)

// World position of the filtered depth at Kinect pixel p, exactly as
// heightMapShader.vert computes it; .w = elevation along the base plane.
vec4 worldAt(vec2 p)
{
    p = clamp(p, kinectROIRect.xy, kinectROIRect.zw);
    float depth = texture2DRect(tex0, p).r * depthTransformation.x + depthTransformation.y;
    vec4 v = kinectWorldMatrix * vec4(p, depth, 1.0);
    v = v * depth;
    v.w = 1.0;
    return vec4(v.xyz, dot(basePlaneEq, v));
}

// Signed slope (rise over horizontal run) from world point a to b.
float riseOverRun(vec4 a, vec4 b)
{
    float rise = b.w - a.w;
    vec3 d = b.xyz - a.xyz;
    float run = sqrt(max(dot(d, d) - rise * rise, 0.0));
    return rise / max(run, 1e-3);
}

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
    // Band fill off (dark-room look): land starts from black; everything
    // drawn on top (soil tint, plants, water, lines) is unchanged.
    if (bandFill == 0)
        color = vec4(0.0, 0.0, 0.0, 1.0);

    vec2 vegUV = (texcoordfrag - vegetationGridOrigin) / vegetationGridStep;
    vec4 veg = (hasVegetation == 1) ? texture2DRect(vegetationSampler, vegUV) : vec4(0.0);
    float waterCode = veg.a * 255.0;
    bool simWater = waterCode > 0.5 && waterCode < 249.5;

    // Water-line derivatives, taken here in uniform control flow (fwidth
    // is undefined inside the per-pixel drawWater branch). They change
    // no output by themselves.
    float wlU = depthfrag * waterLineTransform.x + waterLineTransform.y; // elevation / spacing
    float wlW = fwidth(wlU);           // spacings per projector pixel
    vec2 wlCellsPerPx = fwidth(vegUV); // water-grid cells per projector pixel
    // Land lines: key height (mm) and its change per projector pixel.
    float hMM = depthfrag * heightTransform.x + heightTransform.y;
    float hW = fwidth(hMM);
    // Flat-ground fade: 1 on slopes, 0 where the wide-baseline slope is
    // under half the threshold. Uniform condition, so no derivative issue.
    float flatFade = 1.0;
    if (flatFadeSlope > 0.0 && (drawLandLines == 1 || drawWaterLines == 1))
    {
        vec2 kx = vec2(5.0, 0.0), ky = vec2(0.0, 5.0);
        vec2 d1 = vec2(3.5355, 3.5355), d2 = vec2(3.5355, -3.5355);
        float gx = riseOverRun(worldAt(texcoordfrag - kx), worldAt(texcoordfrag + kx));
        float gy = riseOverRun(worldAt(texcoordfrag - ky), worldAt(texcoordfrag + ky));
        float g1 = riseOverRun(worldAt(texcoordfrag - d1), worldAt(texcoordfrag + d1)); // (gx + gy) / sqrt 2
        float g2 = riseOverRun(worldAt(texcoordfrag - d2), worldAt(texcoordfrag + d2)); // (gx - gy) / sqrt 2
        float slope = length(vec2(0.5 * (gx + 0.70710678 * (g1 + g2)),
                                  0.5 * (gy + 0.70710678 * (g1 - g2))));
        flatFade = smoothstep(0.5 * flatFadeSlope, flatFadeSlope, slope);
    }

    // Soil tint (succession model, 2026-10-28): a subtle brown in
    // proportion to soil S, wherever there is no simulated water.
    if (hasSite == 1 && !simWater)
    {
        float soilS = texture2DRect(siteSampler, vegUV).r;
        color.rgb = mix(color.rgb, vec3(0.42, 0.27, 0.13), soilTintStrength * soilS);
    }

    // Plants: a filled disc with a darker rim per site, over the land
    // colours and soil tint, never on simulated water; contours stay on
    // top. Must match PlantLayer::renderOver() (the headless reference).
    if (hasPlants == 1 && !simWater)
    {
        vec2 mm = vegUV * plantMmPerCell;
        vec2 cell = floor(mm / plantSpacingMM);
        for (int oy = -1; oy <= 1; oy++)
        {
            for (int ox = -1; ox <= 1; ox++)
            {
                vec2 sc = cell + vec2(float(ox), float(oy));
                if (sc.x < 0.0 || sc.y < 0.0 || sc.x >= plantSites.x || sc.y >= plantSites.y)
                    continue;
                vec4 st = texture2DRect(plantSampler, sc + 0.5);
                float role = floor(st.r * 255.0 + 0.5);
                if (role < 0.5)
                    continue;
                float r = st.g * plantRadiusScale;
                float d = length(mm - (sc + st.ba) * plantSpacingMM);
                float plantCover = 1.0 - smoothstep(r - 0.25, r + 0.25, d);
                if (plantCover <= 0.0)
                    continue;
                float rimW = max(0.6, 0.25 * r);
                float rim = smoothstep(r - rimW - 0.25, r - rimW + 0.25, d);
                vec3 fill = vec3(0.788, 0.859, 0.431);               // pioneer #C9DB6E
                vec3 rimColour = vec3(0.545, 0.620, 0.153);          //   rim  #8B9E27
                if (role > 1.5 && role < 2.5) {
                    fill = vec3(0.369, 0.608, 0.298);                // intermediate #5E9B4C
                    rimColour = vec3(0.220, 0.365, 0.180);           //   rim  #385D2E
                } else if (role > 2.5) {
                    fill = vec3(0.184, 0.369, 0.204);                // climax #2F5E34
                    rimColour = vec3(0.110, 0.220, 0.122);           //   rim  #1C381F
                }
                color.rgb = mix(color.rgb, mix(fill, rimColour, rim), plantCover);
            }
        }
    }

    // Glowing land lines (2026-11-06), over the land colours, soil tint and
    // plants, never on drawn simulated water. Must match
    // tools/land_lines_preview.py.
    if (drawLandLines == 1 && !(drawSimWater == 1 && simWater) && hW > 1e-6 && abs(heightTransform.x) > 1e-9)
    {
        float best = 0.0;      // strongest line here
        float bestLevel = 0.0; // its level, mm
        if (landLineMode == 0)
        {
            for (int i = 0; i < 16; i++)
            {
                if (i >= landEdgeCount)
                    break;
                float dPx = abs(hMM - landEdge[i]) / hW; // pixels to this edge's isoline
                float core = clamp(1.0 - dPx, 0.0, 1.0);
                float halo = landLineGlowPx > 0.01 ? 0.6 * exp(-dPx / landLineGlowPx) : 0.0;
                // Fade where this line is closer than 4 px to its neighbour.
                float fade = 1.0 - smoothstep(0.25, 0.5, hW / max(landEdgeGap[i], 0.01));
                float l = clamp(core + halo, 0.0, 1.0) * fade;
                if (l > best) { best = l; bestLevel = landEdge[i]; }
            }
        }
        else
        {
            float sp = max(landLineSpacing, 0.1);
            float u = (hMM - landLineOffset) / sp;
            float k = floor(u + 0.5);
            float uW = hW / sp;
            float dPx = abs(u - k) / uW;
            float core = clamp(1.0 - dPx, 0.0, 1.0);
            float halo = landLineGlowPx > 0.01 ? 0.6 * exp(-dPx / landLineGlowPx) : 0.0;
            best = clamp(core + halo, 0.0, 1.0) * (1.0 - smoothstep(0.25, 0.5, uW));
            bestLevel = landLineOffset + k * sp;
        }
        // The band at the line's own level (+0.5 mm: at a boundary, the band
        // above), so a line keeps one colour along its length.
        vec3 lineColour = texture2DRect(heightColorMapSampler,
            vec2((bestLevel + 0.5 - heightTransform.y) / heightTransform.x, 0.5)).rgb;
        float cover = clamp(veg.r + veg.g + veg.b, 0.0, 1.0); // plant cover (0 without vegetation)
        float gain = landLineBrightness * (1.0 - dimUnderPlants * cover) * flatFade;
        color.rgb = min(color.rgb + lineColour * (gain * best), vec3(1.0));
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

        // Water lines and shoreline glow (2026-11-05), over the water
        // colour above and only here (never on land). A ~1 px core plus
        // an exponential halo of waterLineGlowPx, added and clamped.
        // Must match tools/water_lines_preview.py.
        if (drawWaterLines == 1 || drawShorelineGlow == 1)
        {
            float light = 0.0;
            if (drawWaterLines == 1 && wlW > 1e-6) // flat bed: no isoline, and no division by 0
            {
                float f = fract(wlU);
                float dPx = min(f, 1.0 - f) / wlW; // pixels to the nearest isoline
                float core = clamp(1.0 - dPx, 0.0, 1.0);
                float halo = waterLineGlowPx > 0.01 ? 0.6 * exp(-dPx / waterLineGlowPx) : 0.0;
                // Lines closer than 4 px, gone at 2 px (steep walls), would merge into
                // a solid band; fade them out there.
                float fade = 1.0 - smoothstep(0.25, 0.5, wlW);
                light = clamp(core + halo, 0.0, 1.0) * fade * flatFade;
            }
            if (drawShorelineGlow == 1)
            {
                // Distance (cells) from this pixel to the nearest dry or
                // snow texel within 3 cells, read from the existing water
                // texture at texel centres (filtering unchanged).
                vec2 base = floor(vegUV) + 0.5;
                float dMin = 1e3;
                for (int oy = -3; oy <= 3; oy++)
                {
                    for (int ox = -3; ox <= 3; ox++)
                    {
                        vec2 c = base + vec2(float(ox), float(oy));
                        float code = texture2DRect(vegetationSampler, c).a * 255.0;
                        if (code < 0.5 || code > 249.5)
                            dMin = min(dMin, length(max(abs(vegUV - c) - 0.5, 0.0)));
                    }
                }
                float cellsPerPx = max(max(wlCellsPerPx.x, wlCellsPerPx.y), 1e-6);
                float dPx = dMin / cellsPerPx;
                float core = clamp(1.0 - dPx, 0.0, 1.0);
                float halo = waterLineGlowPx > 0.01 ? 0.6 * exp(-dPx / waterLineGlowPx) : 0.0;
                light = max(light, clamp(core + halo, 0.0, 1.0));
            }
            vec3 lineColour = vec3(0.435, 0.827, 1.0); // #6FD3FF
            color.rgb = min(color.rgb + lineColour * (waterLineBrightness * light), vec3(1.0));
        }
    }

    // Operator debug overlay: moisture M or soil S in false colour.
    if (hasSite == 1 && siteDebugView > 0)
    {
        vec4 site = texture2DRect(siteSampler, vegUV);
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

    gl_FragColor = color;
}
