#include "FmDisplays.h"

#include "AlgoTopology.h"     // shared algorithm modulation graph
#include "FmEnvelopeSim.h"    // envelope stage schematic
#include "PatchParameters.h"  // fmGroupParamId / fmOpParamId
#include "SsgEgBehavior.h"    // decodeSsgEg (SSG-EG loop/hold shapes)

namespace audio_plugin {

int algoCarrierMask(int algo) {
  return opna::algoCarrierMask(algo);
}

// ---------------------------------------------------------------- AlgorithmDisplay
AlgorithmDisplay::AlgorithmDisplay(juce::AudioProcessorValueTreeState& apvts)
    : apvts_(apvts) {
  startTimerHz(20);
}
AlgorithmDisplay::~AlgorithmDisplay() = default;

void AlgorithmDisplay::setGroup(int group) {
  group_ = group;
  idFor_ = nullptr;  // fall back to fmGroupParamId(group_, ...)
  // Read the real values now so the first paint is correct (no flash to alg 0).
  lastAlgo_ = (int)(param("algorithm") + 0.5f);
  lastFeedback_ = (int)(param("feedback") + 0.5f);
  repaint();
}

void AlgorithmDisplay::setParamIds(std::function<juce::String(const char*)> idFor) {
  idFor_ = std::move(idFor);
  lastAlgo_ = (int)(param("algorithm") + 0.5f);
  lastFeedback_ = (int)(param("feedback") + 0.5f);
  repaint();
}

void AlgorithmDisplay::setAccent(juce::Colour c) {
  accent_ = c;
  repaint();
}

float AlgorithmDisplay::param(const char* name) const {
  const juce::String id = idFor_ ? idFor_(name) : fmGroupParamId(group_, name);
  if (auto* p = apvts_.getRawParameterValue(id))
    return p->load();
  return 0.0f;
}

void AlgorithmDisplay::timerCallback() {
  const int a = (int)(param("algorithm") + 0.5f);
  const int f = (int)(param("feedback") + 0.5f);
  if (a != lastAlgo_ || f != lastFeedback_) {
    lastAlgo_ = a;
    lastFeedback_ = f;
    repaint();
  }
}

void AlgorithmDisplay::paint(juce::Graphics& g) {
  auto area = getLocalBounds().toFloat().reduced(6.0f);
  const int algo = juce::jlimit(0, 7, lastAlgo_ < 0 ? 0 : lastAlgo_);
  const opna::AlgoTopology& def = opna::kAlgoTopology[algo];

  // Depth = number of modulation stages between an operator and the output
  // (carriers = 0, a modulator feeding a carrier = 1, ...). Every edge connects
  // operators in adjacent depth rows, so connectors stay within a single row
  // gap and never cross a box. Modulators stack above their carriers (DX7-style).
  int depth[4];
  for (int op = 0; op < 4; ++op)
    depth[op] = (def.carriers & (1 << op)) ? 0 : -1;
  for (int pass = 0; pass < 4; ++pass)
    for (auto& e : def.edges) {
      if (e[0] < 0)
        break;
      if (depth[e[1]] >= 0)
        depth[e[0]] = juce::jmax(depth[e[0]], depth[e[1]] + 1);
    }
  int maxDepth = 0;
  for (int op = 0; op < 4; ++op) {
    if (depth[op] < 0)
      depth[op] = 0;
    maxDepth = juce::jmax(maxDepth, depth[op]);
  }

  // Classic-diagram convention: every operator cell is the SAME fixed width and
  // stays that size across all algorithms. Size it once to fit the densest
  // possible row (all 4 ops parallel, e.g. algo 8) with a gap between cells and
  // a margin each side, then place each row at that fixed pitch, centred. This
  // keeps the boxes equal and separated regardless of the panel's pixel width
  // (width-proportional spacing would let a fixed 30px box collide and fuse into
  // one rectangle at some sizes).
  constexpr int kMaxOps = 4;
  const float gap = 8.0f;
  const float bw =
      juce::jlimit(12.0f, 30.0f,
                   (area.getWidth() - (float)(kMaxOps + 1) * gap) / (float)kMaxOps);
  const float bh = 19.0f;
  const float pitch = bw + gap;
  const float busY = area.getBottom() - 6.0f;
  const float botRow = busY - 16.0f;             // carrier (depth 0) row centre
  const float topRow = area.getY() + bh * 0.5f;  // deepest modulator row centre
  juce::Point<float> centre[4];
  for (int d = 0; d <= maxDepth; ++d) {
    int count = 0;
    for (int op = 0; op < 4; ++op)
      count += (depth[op] == d);
    const float y = (maxDepth == 0)
                        ? botRow
                        : botRow - (botRow - topRow) * (float)d / (float)maxDepth;
    // Centre this row's `count` equal-width cells around the panel mid-line.
    const float rowW = (float)count * bw + (float)(count - 1) * gap;
    float x = area.getCentreX() - rowW * 0.5f + bw * 0.5f;
    for (int op = 0; op < 4; ++op)
      if (depth[op] == d) {
        centre[op] = {x, y};
        x += pitch;
      }
  }

  // --- 1. Modulation connectors: right-angle elbows routed in the row gap. ---
  g.setColour(accent_);
  for (auto& e : def.edges) {
    if (e[0] < 0)
      break;
    const auto from = centre[e[0]];  // modulator (above)
    const auto to = centre[e[1]];    // carrier/target (below)
    const float fy = from.y + bh * 0.5f;
    const float ty = to.y - bh * 0.5f;
    const float midY = (fy + ty) * 0.5f;  // turn at the midpoint of the row gap
    g.drawLine(from.x, fy, from.x, midY, 1.5f);
    if (std::abs(from.x - to.x) > 0.5f)
      g.drawLine(from.x, midY, to.x, midY, 1.5f);
    g.drawLine(to.x, midY, to.x, ty, 1.5f);
  }

  // --- 2. Carrier output drops + shared horizontal output bus. ---
  float busL = area.getRight(), busR = area.getX();
  for (int op = 0; op < 4; ++op)
    if (def.carriers & (1 << op)) {
      g.drawLine(centre[op].x, centre[op].y + bh * 0.5f, centre[op].x, busY, 1.5f);
      busL = juce::jmin(busL, centre[op].x);
      busR = juce::jmax(busR, centre[op].x);
    }
  g.drawLine(busL, busY, busR, busY, 1.5f);

  // --- 3. Feedback self-loop on OP1 (the chip's feedback operator). ---
  if (lastFeedback_ > 0) {
    g.setColour(OpnaColours::amber);
    const float rx = centre[0].x + bw * 0.5f;
    const float ty = centre[0].y - bh * 0.5f;
    juce::Path fb;  // small square bracket looping out the top-right and back in
    fb.startNewSubPath(centre[0].x + 3.0f, ty);
    fb.lineTo(centre[0].x + 3.0f, ty - 6.0f);
    fb.lineTo(rx + 6.0f, ty - 6.0f);
    fb.lineTo(rx + 6.0f, centre[0].y);
    fb.lineTo(rx, centre[0].y);
    g.strokePath(fb, juce::PathStrokeType(1.4f));
  }

  // --- 4. Operator boxes LAST (drawn on top so no line overlaps a box). ---
  g.setFont(OpnaLookAndFeel::font(13.0f));
  for (int op = 0; op < 4; ++op) {
    const bool carrier = (def.carriers & (1 << op)) != 0;
    juce::Rectangle<float> b(centre[op].x - bw * 0.5f, centre[op].y - bh * 0.5f,
                             bw, bh);
    g.setColour(carrier ? accent_ : OpnaColours::panel);
    g.fillRect(b);
    g.setColour(accent_);
    g.drawRect(b, 1.0f);
    g.setColour(carrier ? OpnaColours::bg : OpnaColours::ink);
    g.drawText(juce::String(op + 1), b, juce::Justification::centred);
  }
}

// ----------------------------------------------------------------- EnvelopeDisplay
EnvelopeDisplay::EnvelopeDisplay(juce::AudioProcessorValueTreeState& apvts)
    : apvts_(apvts) {
  startTimerHz(20);
}
EnvelopeDisplay::~EnvelopeDisplay() = default;

void EnvelopeDisplay::setTarget(int group, int op) {
  group_ = group;
  opIndex_ = op;
  signature_ = -1;
  repaint();
}

void EnvelopeDisplay::setAccent(juce::Colour c) {
  accent_ = c;
  repaint();
}

int EnvelopeDisplay::op(const char* field) const {
  if (auto* p = apvts_.getRawParameterValue(fmOpParamId(group_, opIndex_, field)))
    return (int)(p->load() + 0.5f);
  return 0;
}

// FM operating rate (clock / 144 at prescale 6), which the envelope timing scales
// with. The crystal is fixed (ChipRates default); only the prescale varies.
static double fmRateFromPrescale(juce::AudioProcessorValueTreeState& apvts) {
  int prescale = 6;
  if (auto* p = apvts.getRawParameterValue("chip_prescale")) {
    const int idx = (int)(p->load() + 0.5f);  // choice 0/1/2 -> 6/3/2
    prescale = idx == 2 ? 2 : (idx == 1 ? 3 : 6);
  }
  return 7987200.0 / (24.0 * (double)prescale);
}

void EnvelopeDisplay::timerCallback() {
  int prescaleIdx = 0;
  if (auto* p = apvts_.getRawParameterValue("chip_prescale"))
    prescaleIdx = (int)(p->load() + 0.5f);
  const long long sig =
      (long long)op("ar") | ((long long)op("dr") << 5) |
      ((long long)op("sr") << 10) | ((long long)op("rr") << 15) |
      ((long long)op("sl") << 19) | ((long long)op("tl") << 23) |
      ((long long)op("ssgeg") << 30) | ((long long)prescaleIdx << 34);
  if (sig != signature_) {
    signature_ = sig;
    repaint();
  }
}

void EnvelopeDisplay::paint(juce::Graphics& g) {
  auto b = getLocalBounds().toFloat().reduced(2.0f);
  g.setColour(OpnaColours::bg);
  g.fillRect(b);
  g.setColour(OpnaColours::line);
  g.drawRect(b, 1.0f);

  // Stage schematic (see FmEnvelopeSim): each stage's width is proportional to its
  // real duration but min-clamped, the sustain window is fixed, and levels are
  // schematic so every parameter maps to one predictable thing.
  opna::EnvInput in;
  in.ar = op("ar");
  in.dr = op("dr");
  in.sr = op("sr");
  in.rr = op("rr");
  in.slChip = 15 - op("sl");  // APVTS "sl" is inverted (higher = louder)
  in.ssgeg = op("ssgeg");

  const opna::EnvSchematic env =
      opna::buildEnvSchematic(in, fmRateFromPrescale(apvts_));

  auto plot = b.reduced(1.0f);
  auto axis = plot.removeFromBottom(11.0f);  // strip for A / D / S / R stage labels

  // Schematic levels (linear 0..1). Peak height is a linear function of TL; the
  // sustain level is a linear function of SL; SR tilts the sustain segment.
  const float peak = (127.0f - (float)op("tl")) / 127.0f;
  const float susLevel = peak * ((float)op("sl") / 15.0f);
  const float susEnd = susLevel * (1.0f - env.sustainDrop);

  auto Y = [&](float level) {
    return plot.getBottom() -
           juce::jlimit(0.0f, 1.0f, level) * (plot.getHeight() - 2.0f);
  };

  // Faint level gridlines at 1/4 / 1/2 / 3/4.
  g.setColour(OpnaColours::line.withAlpha(0.4f));
  for (int q = 1; q < 4; ++q)
    g.drawLine(plot.getX(), Y(q / 4.0f), plot.getRight(), Y(q / 4.0f), 1.0f);

  // Stage widths: fixed sustain window, the rest split among attack/decay/release
  // proportional to their real durations but with a per-stage minimum so a fast
  // stage never collapses to nothing.
  const float W = plot.getWidth();
  const float susW = 0.18f * W;
  const float minW = 0.10f * W;
  const double sumD =
      env.attack.seconds + env.decay.seconds + env.release.seconds;
  const float propBudget = W - susW - 3.0f * minW;
  auto stageW = [&](double secs) {
    const float prop =
        sumD > 0.0 ? propBudget * (float)(secs / sumD) : propBudget / 3.0f;
    return minW + prop;
  };
  // Attack + decay + sustain consume their widths; release fills the remainder
  // (which equals its own min-clamped proportional width, since the parts sum to W).
  const float wA = stageW(env.attack.seconds);
  const float wD = stageW(env.decay.seconds);

  const float xA = plot.getX();
  const float xD = xA + wA;
  const float xS = xD + wD;
  const float xR = xS + susW;

  // Stage dividers.
  g.setColour(OpnaColours::line.withAlpha(0.35f));
  for (float x : {xD, xS, xR})
    g.drawLine(x, plot.getY(), x, plot.getBottom(), 1.0f);

  // Draw one stage's authentic curve, renormalising its amplitude range onto the
  // schematic [startLevel, endLevel] across [x0, x1].
  juce::Path p;
  bool started = false;
  auto drawStage = [&](const opna::EnvStage& st, float x0, float x1, float lvl0,
                       float lvl1) {
    const auto& sh = st.shape;
    const int n = (int)sh.size();
    if (n == 0)
      return;
    const float a0 = sh.front(), a1 = sh.back();
    const float denom = a1 - a0;
    for (int i = 0; i < n; ++i) {
      const float fx = n > 1 ? (float)i / (float)(n - 1) : 0.0f;
      const float norm =
          std::abs(denom) > 1.0e-6f ? (sh[(size_t)i] - a0) / denom : fx;
      const float x = x0 + (x1 - x0) * fx;
      const float y = Y(lvl0 + (lvl1 - lvl0) * norm);
      if (!started) {
        p.startNewSubPath(x, y);
        started = true;
      } else {
        p.lineTo(x, y);
      }
    }
  };

  drawStage(env.attack, xA, xD, 0.0f, peak);
  if (env.ssgOn) {
    // SSG-EG: the decay+sustain region loops or holds instead of settling. Draw
    // the mode's pattern between the peak and a near-silent floor (the chip loops
    // off the 0x200 = ~-48 dB point). Inverted modes (4/6) flip the loop. At
    // key-off the chip forces silence, so the release is an instant drop.
    const SsgEgBehavior eg = decodeSsgEg(in.ssgeg);
    const float floorL = peak * 0.03f;
    const int cycles = juce::jlimit(2, 6, 2 + op("dr") / 8);
    auto sx = [&](float f) {
      return xD + (xR - xD) * juce::jlimit(0.0f, 1.0f, f);
    };
    auto emit = [&](float f, float lvl) {
      p.lineTo(sx(f), Y(eg.startInverted ? (peak + floorL - lvl) : lvl));
    };
    if (eg.loops && eg.alternates) {  // triangle: peak <-> floor
      bool down = true;
      emit(0.0f, peak);
      for (int i = 0; i < cycles; ++i) {
        emit((float)(i + 1) / cycles, down ? floorL : peak);
        down = !down;
      }
    } else if (eg.loops) {  // sawtooth: peak -> floor, jump back up
      for (int i = 0; i < cycles; ++i) {
        emit((float)i / cycles, peak);
        emit((float)(i + 1) / cycles, floorL);
      }
    } else if (eg.holdsHigh) {  // run once, dip, snap to full and hold
      p.lineTo(sx(0.0f), Y(peak));
      p.lineTo(sx(1.0f / 3), Y(floorL));
      p.lineTo(sx(2.0f / 3), Y(peak));
      p.lineTo(sx(1.0f), Y(peak));
    } else {  // holdsLow: run once down to silence, hold
      p.lineTo(sx(0.0f), Y(peak));
      p.lineTo(sx(1.0f / 3), Y(0.0f));
      p.lineTo(sx(1.0f), Y(0.0f));
    }
    p.lineTo(xR + (plot.getRight() - xR) * 0.2f, Y(0.0f));  // forced-silent release
    p.lineTo(plot.getRight(), Y(0.0f));
  } else {
    drawStage(env.decay, xD, xS, peak, susLevel);
    drawStage(env.sustain, xS, xR, susLevel, susEnd);  // D2R curve (knee shows SR)
    drawStage(env.release, xR, plot.getRight(), susEnd, 0.0f);
  }

  g.setColour(accent_);
  g.strokePath(p, juce::PathStrokeType(1.8f));

  // Stage labels along the bottom strip.
  g.setFont(OpnaLookAndFeel::font(10.0f));
  g.setColour(OpnaColours::ink);
  struct Lbl { const char* t; float x0, x1; };
  for (auto& l : {Lbl{"A", xA, xD}, Lbl{"D", xD, xS}, Lbl{"S", xS, xR},
                  Lbl{"R", xR, plot.getRight()}})
    g.drawText(l.t, (int)l.x0, (int)axis.getY(), (int)(l.x1 - l.x0),
               (int)axis.getHeight(), juce::Justification::centred, false);

  // "SSG-EG ON" badge whenever the enable bit is set (ssgeg >= 8).
  if (env.ssgOn) {
    g.setColour(accent_);
    g.setFont(OpnaLookAndFeel::font(11.0f));
    g.drawText("SSG-EG ON", plot.reduced(4, 3), juce::Justification::topRight);
  }
}

}  // namespace audio_plugin
