#include "ModernLookAndFeel.h"

ModernLookAndFeel::ModernLookAndFeel()
{
    // Base palette - dark professional theme
    setColour(juce::Slider::backgroundColourId, juce::Colour(0xff1a1d29));
    setColour(juce::Slider::trackColourId, juce::Colour(0xff262b3d));
    setColour(juce::Slider::thumbColourId, juce::Colour(0xff00d4ff));
    setColour(juce::Slider::rotarySliderFillColourId, juce::Colour(0xff00d4ff));
    setColour(juce::Slider::rotarySliderOutlineColourId, juce::Colour(0xff262b3d));
    setColour(juce::Slider::textBoxBackgroundColourId, juce::Colour(0xff14161f));
    setColour(juce::Slider::textBoxTextColourId, juce::Colours::white);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0xff2a2e42));
    setColour(juce::Slider::textBoxHighlightColourId, juce::Colour(0xff00d4ff));

    setColour(juce::ComboBox::backgroundColourId, juce::Colour(0xff1a1d29));
    setColour(juce::ComboBox::textColourId, juce::Colours::white);
    setColour(juce::ComboBox::outlineColourId, juce::Colour(0xff2a2e42));
    setColour(juce::ComboBox::buttonColourId, juce::Colour(0xff00d4ff));
    setColour(juce::ComboBox::focusedOutlineColourId, juce::Colour(0xff00d4ff));

    setColour(juce::ToggleButton::textColourId, juce::Colours::white);
    setColour(juce::ToggleButton::tickColourId, juce::Colour(0xff00ff88));
    setColour(juce::ToggleButton::tickDisabledColourId, juce::Colour(0xff555a70));

    setColour(juce::Label::textColourId, juce::Colour(0xffaab0c5));
    setColour(juce::Label::textWhenEditingColourId, juce::Colours::white);
    setColour(juce::Label::backgroundWhenEditingColourId, juce::Colour(0xff14161f));
}

juce::Font ModernLookAndFeel::getLabelFont(juce::Label&)
{
    return juce::Font(11.0f, juce::Font::plain);
}

juce::Font ModernLookAndFeel::getTextButtonFont(juce::TextButton&, int buttonHeight)
{
    if (buttonHeight > 24) return juce::Font(14.0f, juce::Font::bold);
    return juce::Font(12.0f, juce::Font::bold);
}

juce::Font ModernLookAndFeel::getComboBoxFont(juce::ComboBox&)
{
    return juce::Font(11.0f, juce::Font::plain);
}

// ---- Vertical gain fader: slim track + glow thumb + centre detent ----
void ModernLookAndFeel::drawLinearSlider(juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPos, float /*minSliderPos*/, float /*maxSliderPos*/,
                                         const juce::Slider::SliderStyle style,
                                         juce::Slider& slider)
{
    if (style != juce::Slider::LinearVertical && style != juce::Slider::LinearHorizontal)
    {
        LookAndFeel_V4::drawLinearSlider(g, x, y, width, height, sliderPos, 0, 0, style, slider);
        return;
    }

    const bool isVertical = (style == juce::Slider::LinearVertical);
    const float trackThickness = 5.0f;
    juce::Rectangle<float> track;
    if (isVertical)
    {
        const float cx = x + width * 0.5f;
        track = { cx - trackThickness * 0.5f, (float) y + 4.0f, trackThickness, (float) height - 8.0f };
    }
    else
    {
        const float cy = y + height * 0.5f;
        track = { (float) x + 4.0f, cy - trackThickness * 0.5f, (float) width - 8.0f, trackThickness };
    }

    // Track background
    g.setColour(findColour(juce::Slider::trackColourId));
    g.fillRoundedRectangle(track, trackThickness * 0.5f);

    // Fill from centre detent (0 dB) to thumb - bipolar fader look
    auto range = slider.getRange();
    const double centreVal = 0.0;
    const double proportionCentre = (centreVal - range.getStart()) / (range.getLength() != 0 ? range.getLength() : 1.0);
    const double proportionPos = slider.valueToProportionOfLength(slider.getValue());
    float fillStart, fillEnd;
    if (isVertical)
    {
        const float yCentre = track.getBottom() - (float) proportionCentre * track.getHeight();
        const float yPos = track.getBottom() - (float) proportionPos * track.getHeight();
        fillStart = juce::jmin(yCentre, yPos);
        fillEnd = juce::jmax(yCentre, yPos);
        juce::Rectangle<float> fill(track.getX(), fillStart, track.getWidth(), fillEnd - fillStart + 0.5f);
        juce::ColourGradient grad(juce::Colour(0xff00d4ff), fill.getCentreX(), fill.getY(),
                                  juce::Colour(0xff0088cc), fill.getCentreX(), fill.getBottom(), false);
        g.setGradientFill(grad);
        g.fillRoundedRectangle(fill, trackThickness * 0.5f);
        // Centre detent tick
        g.setColour(juce::Colour(0xff3a3f55));
        g.fillRect(track.getX() - 3.0f, yCentre - 0.75f, track.getWidth() + 6.0f, 1.5f);
    }
    else
    {
        const float xCentre = track.getX() + (float) proportionCentre * track.getWidth();
        const float xPos = track.getX() + (float) proportionPos * track.getWidth();
        fillStart = juce::jmin(xCentre, xPos);
        fillEnd = juce::jmax(xCentre, xPos);
        juce::Rectangle<float> fill(fillStart, track.getY(), fillEnd - fillStart + 0.5f, track.getHeight());
        g.setColour(findColour(juce::Slider::thumbColourId));
        g.fillRoundedRectangle(fill, trackThickness * 0.5f);
    }

    // Thumb: glowing circle
    const float thumbR = isVertical ? 9.0f : 8.0f;
    juce::Point<float> thumbCentre;
    if (isVertical)
        thumbCentre = { track.getCentreX(), juce::jlimit(track.getY(), track.getBottom(), sliderPos) };
    else
        thumbCentre = { juce::jlimit(track.getX(), track.getRight(), sliderPos), track.getCentreY() };

    // Glow
    g.setColour(juce::Colour(0x5500d4ff));
    g.fillEllipse(thumbCentre.x - thumbR - 3.0f, thumbCentre.y - thumbR - 3.0f,
                  (thumbR + 3.0f) * 2.0f, (thumbR + 3.0f) * 2.0f);
    // Body
    juce::ColourGradient body(juce::Colours::white, thumbCentre.x - 3, thumbCentre.y - 3,
                              findColour(juce::Slider::thumbColourId), thumbCentre.x + 4, thumbCentre.y + 4, true);
    g.setGradientFill(body);
    g.fillEllipse(thumbCentre.x - thumbR, thumbCentre.y - thumbR, thumbR * 2.0f, thumbR * 2.0f);
    g.setColour(juce::Colour(0xff0a0c12).withAlpha(0.6f));
    g.drawEllipse(thumbCentre.x - thumbR, thumbCentre.y - thumbR, thumbR * 2.0f, thumbR * 2.0f, 1.2f);
    // White core dot
    g.setColour(juce::Colours::white.withAlpha(0.9f));
    g.fillEllipse(thumbCentre.x - 2.5f, thumbCentre.y - 2.5f, 5.0f, 5.0f);
}

// ---- Rotary knob: dark body + colored arc + needle ----
void ModernLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                         float sliderPosProportional, float rotaryStartAngle,
                                         float rotaryEndAngle, juce::Slider& slider)
{
    const float diameter = juce::jmin((float) width, (float) height) - 8.0f;
    const float radius = diameter * 0.5f;
    const juce::Point<float> centre((float) x + width * 0.5f, (float) y + height * 0.5f);

    // Drop shadow
    g.setColour(juce::Colours::black.withAlpha(0.45f));
    g.fillEllipse(centre.x - radius + 1.5f, centre.y - radius + 2.5f, radius * 2.0f, radius * 2.0f);

    // Body
    juce::ColourGradient body(juce::Colour(0xff2b3044), centre.x, centre.y - radius,
                              juce::Colour(0xff14161f), centre.x, centre.y + radius, false);
    g.setGradientFill(body);
    g.fillEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f);
    g.setColour(juce::Colour(0xff3a4058));
    g.drawEllipse(centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f, 1.2f);

    // Value arc
    const float angle = rotaryStartAngle + sliderPosProportional * (rotaryEndAngle - rotaryStartAngle);
    juce::Path arc;
    arc.addCentredArc(centre.x, centre.y, radius - 3.0f, radius - 3.0f,
                      0.0f, rotaryStartAngle, angle, true);
    g.setColour(slider.findColour(juce::Slider::thumbColourId).withAlpha(0.95f));
    g.strokePath(arc, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                           juce::PathStrokeType::rounded));
    // Track remainder
    juce::Path rest;
    rest.addCentredArc(centre.x, centre.y, radius - 3.0f, radius - 3.0f,
                       0.0f, angle, rotaryEndAngle, true);
    g.setColour(juce::Colour(0xff343a52));
    g.strokePath(rest, juce::PathStrokeType(3.0f, juce::PathStrokeType::curved,
                                            juce::PathStrokeType::rounded));

    // Needle
    juce::Path needle;
    const float needleLen = radius - 6.0f;
    juce::Point<float> tip(centre.x + std::sin(angle) * needleLen,
                           centre.y - std::cos(angle) * needleLen);
    needle.startNewSubPath(centre);
    needle.lineTo(tip);
    g.setColour(juce::Colours::white);
    g.strokePath(needle, juce::PathStrokeType(2.2f, juce::PathStrokeType::curved,
                                              juce::PathStrokeType::rounded));
    // Hub
    g.setColour(juce::Colour(0xff0d0f16));
    g.fillEllipse(centre.x - 3.5f, centre.y - 3.5f, 7.0f, 7.0f);
    g.setColour(slider.findColour(juce::Slider::thumbColourId));
    g.drawEllipse(centre.x - 3.5f, centre.y - 3.5f, 7.0f, 7.0f, 1.0f);
}

// ---- Rounded dark combo box with cyan arrow ----
void ModernLookAndFeel::drawComboBox(juce::Graphics& g, int width, int height, bool /*isButtonDown*/,
                                     int buttonX, int buttonY, int buttonW, int buttonH,
                                     juce::ComboBox& box)
{
    juce::Rectangle<float> area(0.5f, 0.5f, (float) width - 1.0f, (float) height - 1.0f);
    const float corner = 5.0f;
    g.setColour(findColour(juce::ComboBox::backgroundColourId));
    g.fillRoundedRectangle(area, corner);
    g.setColour(box.hasKeyboardFocus(true) ? findColour(juce::ComboBox::focusedOutlineColourId)
                                           : findColour(juce::ComboBox::outlineColourId));
    g.drawRoundedRectangle(area, corner, box.hasKeyboardFocus(true) ? 1.5f : 1.0f);

    // Arrow button zone
    juce::Rectangle<float> arrowZone((float) buttonX, (float) buttonY, (float) buttonW, (float) buttonH);
    juce::Path arrow;
    const float cx = arrowZone.getCentreX();
    const float cy = arrowZone.getCentreY();
    arrow.startNewSubPath(cx - 4.0f, cy - 1.5f);
    arrow.lineTo(cx, cy + 2.5f);
    arrow.lineTo(cx + 4.0f, cy - 1.5f);
    g.setColour(findColour(juce::ComboBox::buttonColourId));
    g.strokePath(arrow, juce::PathStrokeType(1.8f, juce::PathStrokeType::curved,
                                             juce::PathStrokeType::rounded));
}

// ---- Pill-style ON/OFF switch ----
void ModernLookAndFeel::drawToggleButton(juce::Graphics& g, juce::ToggleButton& button,
                                         bool /*highlighted*/, bool /*down*/)
{
    const bool isOn = button.getToggleState();
    auto bounds = button.getLocalBounds().toFloat().reduced(1.0f, 3.0f);
    const float corner = bounds.getHeight() * 0.5f;

    g.setColour(isOn ? juce::Colour(0xff123a2a) : juce::Colour(0xff20242f));
    g.fillRoundedRectangle(bounds, corner);
    g.setColour(isOn ? juce::Colour(0xff00ff88) : juce::Colour(0xff3a4058));
    g.drawRoundedRectangle(bounds, corner, 1.2f);

    const float knobD = bounds.getHeight() - 4.0f;
    const float knobX = isOn ? bounds.getRight() - knobD - 2.0f : bounds.getX() + 2.0f;
    juce::Rectangle<float> knob(knobX, bounds.getY() + 2.0f, knobD, knobD);
    if (isOn)
    {
        g.setColour(juce::Colour(0x5500ff88));
        g.fillEllipse(knob.expanded(2.5f));
    }
    g.setColour(isOn ? juce::Colour(0xff00ff88) : juce::Colour(0xff6a7186));
    g.fillEllipse(knob);
    g.setColour(juce::Colours::white.withAlpha(isOn ? 0.9f : 0.5f));
    g.fillEllipse(knob.getCentreX() - 1.5f, knob.getCentreY() - 1.5f, 3.0f, 3.0f);
}
