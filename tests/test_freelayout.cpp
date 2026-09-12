// Free-edition layout audit: crea l'editor headless (no display: solo bounds)
// e verifica che ogni componente visibile stia dentro il genitore.
#include "PluginEditor.h"
#include <cstdio>

static int fails = 0;
static int checked = 0;
#define CHECK(cond, ...) do { \
    if (!(cond)) { ++fails; printf("FAIL: "); printf(__VA_ARGS__); printf("\n"); } \
    else { ++checked; } } while (0)

static juce::String describe(juce::Component* c)
{
    juce::String s = c->getName().isNotEmpty() ? c->getName() : typeid(*c).name();
    if (auto* l = dynamic_cast<juce::Label*>(c))
        s += " \"" + l->getText().substring(0, 24) + "\"";
    if (auto* b = dynamic_cast<juce::Button*>(c))
        s += " \"" + b->getButtonText() + "\"";
    return s + " " + c->getBounds().toString();
}

static void audit(juce::Component* c, int depth = 0, bool inScroller = false)
{
    for (auto* ch : c->getChildren())
    {
        if (! ch->isVisible()) continue;
        auto b = ch->getBounds();
        // Il contenuto scrollabile (viewed component) e' legittimamente piu'
        // largo/alto del viewport: salta il check inside per quel sottoalbero
        bool scrollerHere = inScroller || dynamic_cast<juce::Viewport*>(ch) != nullptr
                         || dynamic_cast<juce::Viewport*>(c) != nullptr;
        bool inside = scrollerHere
            || (b.getX() >= 0 && b.getY() >= 0
                && b.getRight() <= c->getWidth() + 1
                && b.getBottom() <= c->getHeight() + 1);
        bool sane = b.getWidth() > 0 && b.getHeight() > 0;
        if (depth < 4)
            printf("%*s%s %s\n", depth * 2, "", inside ? "ok " : "OUT",
                   describe(ch).toRawUTF8());
        CHECK(sane, "sane size %s", describe(ch).toRawUTF8());
        CHECK(inside, "inside parent %s in %dx%d", describe(ch).toRawUTF8(),
              c->getWidth(), c->getHeight());
        audit(ch, depth + 1, scrollerHere);
    }
}

int main()
{
    SmartEQAudioProcessor proc;
    proc.prepareToPlay(48000, 512);
    std::unique_ptr<juce::AudioProcessorEditor> ed(proc.createEditor());
    printf("editor=%s floating=%d\n", ed->getBounds().toString().toRawUTF8(),
           (int) ed->isOnDesktop());
#ifdef SMARTEQ_FREE_VERSION
    CHECK(ed->getHeight() == 700, "free window 1280x700 (h=%d)", ed->getHeight());
#else
    CHECK(ed->getHeight() == 860, "full window 1280x860 (h=%d)", ed->getHeight());
#endif
    // Pannello curva presente e visibile con area reale (entrambe le edizioni)
    bool curveFound = false;
    for (auto* ch : ed->getChildren())
        if (ch->isVisible() && ch->getWidth() > 1000 && ch->getHeight() > 100
            && dynamic_cast<juce::Viewport*>(ch) == nullptr)
            curveFound = true;
    CHECK(curveFound, "graphic curve panel present");
    audit(ed.get());
    printf("checked=%d fails=%d\n", checked, fails);
    printf(fails == 0 ? "FREELAYOUT ALL OK\n" : "FREELAYOUT %d FAILURES\n", fails);
    return fails;
}
