#pragma once
// EX2-Voice editor - tabs: VOICE | PIANO ROLL | AI LYRICIST. Theme-aware,
// soothe-inspired rounded look via SSLookAndFeel.
#include "PluginProcessor.h"

namespace ss
{

juce::MidiFile buildMidiFromSequence (VocalSequence&, double bpm);
int importMidiIntoSequence (const juce::File&, VocalSequence&);

//==============================================================================
/** Rounded, soft-bordered controls; colors follow the active Theme. */
class SSLookAndFeel : public juce::LookAndFeel_V4
{
public:
    const Theme* th = nullptr;

    void drawButtonBackground (juce::Graphics& g, juce::Button& b,
                               const juce::Colour&, bool over, bool down) override
    {
        auto r = b.getLocalBounds().toFloat().reduced (1.5f);
        const auto panel  = th != nullptr ? th->panel  : juce::Colour (0xff232033);
        const auto line   = th != nullptr ? th->panelLine : juce::Colours::grey;
        const auto accent = th != nullptr ? th->accent : juce::Colours::hotpink;

        auto fill = panel;
        if (b.getToggleState()) fill = accent.withAlpha (0.85f);
        else if (down)          fill = accent.withAlpha (0.7f);
        else if (over)          fill = panel.overlaidWith (accent.withAlpha (0.18f));

        g.setColour (fill);
        g.fillRoundedRectangle (r, 9.0f);
        g.setColour (over || down ? accent.withAlpha (0.9f) : line);
        g.drawRoundedRectangle (r, 9.0f, 1.2f);
    }

    juce::Font getTextButtonFont (juce::TextButton&, int h) override
    {
        return juce::Font (juce::FontOptions (juce::jmin (14.0f, (float) h * 0.5f)));
    }

    void drawComboBox (juce::Graphics& g, int w, int h, bool,
                       int, int, int, int, juce::ComboBox& box) override
    {
        auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (1.0f);
        g.setColour (box.findColour (juce::ComboBox::backgroundColourId));
        g.fillRoundedRectangle (r, 8.0f);
        g.setColour (box.findColour (juce::ComboBox::outlineColourId));
        g.drawRoundedRectangle (r, 8.0f, 1.1f);

        juce::Path p;
        const auto ar = juce::Rectangle<float> ((float) w - 21.0f, (float) h * 0.42f, 11.0f, (float) h * 0.18f);
        p.startNewSubPath (ar.getX(), ar.getY());
        p.lineTo (ar.getCentreX(), ar.getBottom());
        p.lineTo (ar.getRight(), ar.getY());
        g.setColour (box.findColour (juce::ComboBox::arrowColourId));
        g.strokePath (p, juce::PathStrokeType (1.8f));
    }
};

//==============================================================================
class PianoRollCanvas : public juce::Component,
                        private juce::Timer
{
public:
    explicit PianoRollCanvas (SingScribeProcessor& p) : proc (p)
    {
        setWantsKeyboardFocus (true);
        lyricEdit.setVisible (false);
        lyricEdit.onReturnKey = [this] { commitLyricEdit(); };
        lyricEdit.onEscapeKey = [this] { lyricEdit.setVisible (false); };
        lyricEdit.onFocusLost = [this] { commitLyricEdit(); };
        addChildComponent (lyricEdit);
        updateCanvasSize();
        startTimerHz (15);
    }

    void paint (juce::Graphics&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp   (const juce::MouseEvent&) override;
    void mouseDoubleClick (const juce::MouseEvent&) override;
    bool keyPressed (const juce::KeyPress&) override;

    static constexpr int   pitchLo = 36, pitchHi = 96;
    static constexpr int   rowH = 14;
    static constexpr float beatToX = 28.0f;

    float  beatToPx (double b) const { return (float) (b * beatToX); }
    double pxToBeat (float x)  const { return x / beatToX; }
    int    pitchToY (int p)    const { return (pitchHi - p) * rowH; }
    int    yToPitch (int y)    const { return juce::jlimit (pitchLo, pitchHi, pitchHi - y / rowH); }

private:
    void timerCallback() override
    {
        const int rev = proc.sequence.getRevision();
        if (rev != lastSeenRev) { lastSeenRev = rev; updateCanvasSize(); }
        repaint();
    }

    void updateCanvasSize()
    {
        double lastEnd = 16.0;
        for (const auto& n : proc.sequence.snapshot())
            lastEnd = juce::jmax (lastEnd, n.endBeat());
        setSize ((int) ((lastEnd + 8.0) * beatToX), (pitchHi - pitchLo + 1) * rowH);
    }

    const VocalNote* hitTest (juce::Point<int> pos, const std::vector<VocalNote>& notes) const
    {
        for (auto& n : notes)
        {
            juce::Rectangle<float> r (beatToPx (n.startBeat), (float) pitchToY (n.midiPitch),
                                      (float) (n.lengthBeats * beatToX), (float) rowH);
            if (r.contains (pos.toFloat())) return &n;
        }
        return nullptr;
    }

    void commitLyricEdit();
    void auditionSelected();
    static double snap (double b) { return std::round (b * 4.0) / 4.0; }

    SingScribeProcessor& proc;
    juce::TextEditor lyricEdit;
    juce::Uuid selectedId, editingId;
    enum class DragMode { none, move, resize };
    DragMode dragMode = DragMode::none;
    bool dragChanged = false;
    VocalNote dragOrig;
    double grabBeatOffset = 0.0;
    int lastSeenRev = -1;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (PianoRollCanvas)
};

//==============================================================================
class MidiDragOutSpot : public juce::Component
{
public:
    explicit MidiDragOutSpot (SingScribeProcessor& p) : proc (p)
    {
        setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    }
    void paint (juce::Graphics& g) override
    {
        const auto& th = proc.theme();
        auto r = getLocalBounds().toFloat().reduced (1.0f);
        g.setColour (th.panel);
        g.fillRoundedRectangle (r, 9.0f);
        g.setColour (th.accent2.withAlpha (hover ? 0.95f : 0.55f));
        g.drawRoundedRectangle (r, 9.0f, 1.3f);
        g.setColour (th.text.withAlpha (0.9f));
        g.setFont (juce::Font (juce::FontOptions (13.0f)));
        g.drawText ("DRAG MIDI OUT", getLocalBounds(), juce::Justification::centred);
    }
    void mouseEnter (const juce::MouseEvent&) override { hover = true;  repaint(); }
    void mouseExit  (const juce::MouseEvent&) override { hover = false; repaint(); }
    void mouseUp    (const juce::MouseEvent&) override { dragging = false; }
    void mouseDrag (const juce::MouseEvent& e) override
    {
        if (dragging || e.getDistanceFromDragStart() < 8) return;
        dragging = true;
        auto f = juce::File::getSpecialLocation (juce::File::tempDirectory)
                     .getChildFile ("EX2 Voice.mid");
        f.deleteFile();
        juce::FileOutputStream os (f);
        if (os.openedOk() && buildMidiFromSequence (proc.sequence, proc.lastBpm.load()).writeTo (os))
        {
            os.flush();
            juce::DragAndDropContainer::performExternalDragDropOfFiles (
                { f.getFullPathName() }, false, this, nullptr);
        }
    }
private:
    SingScribeProcessor& proc;
    bool hover = false, dragging = false;
};

//==============================================================================
class RollTab : public juce::Component,
                private juce::Timer
{
public:
    explicit RollTab (SingScribeProcessor& p)
        : proc (p), canvas (p), dragOut (p)
    {
        viewport.setViewedComponent (&canvas, false);
        viewport.setScrollBarsShown (true, true);
        addAndMakeVisible (viewport);

        lyricInput.setTextToShowWhenEmpty ("type lyrics - one syllable per note (\"-\" = hold). English or romaji/kana.",
                                           juce::Colours::grey);
        addAndMakeVisible (lyricInput);

        applyBtn.onClick = [this] { applyLyrics(); };
        addAndMakeVisible (applyBtn);

        translateBtn.onClick = [this] { translateLyrics(); };
        addAndMakeVisible (translateBtn);

        addAndMakeVisible (dragOut);
        addAndMakeVisible (status);
        startTimerHz (5);
    }

    void paint (juce::Graphics& g) override
    {
        const auto& th = proc.theme();
        g.setColour (th.panel.withAlpha (0.65f));
        g.fillRoundedRectangle (barArea.toFloat().expanded (4.0f, 2.0f), 12.0f);
        g.setColour (th.panelLine.withAlpha (0.6f));
        g.drawRoundedRectangle (barArea.toFloat().expanded (4.0f, 2.0f), 12.0f, 1.0f);
    }

    void resized() override
    {
        auto r = getLocalBounds();
        auto bar = r.removeFromBottom (84).reduced (10, 6);
        barArea = bar;
        viewport.setBounds (r.reduced (6, 0).withTrimmedTop (4));

        bar.reduce (8, 6);
        auto row1 = bar.removeFromTop (30);
        lyricInput.setBounds (row1.removeFromLeft (juce::jmax (180, row1.getWidth() - 470)));
        applyBtn.setBounds     (row1.removeFromLeft (122).reduced (4, 0));
        translateBtn.setBounds (row1.removeFromLeft (190).reduced (4, 0));
        dragOut.setBounds      (row1.removeFromLeft (150).reduced (2, 0));
        status.setBounds (bar.removeFromTop (26));
    }

private:
    void timerCallback() override
    {
        if (! scrolledOnce && viewport.getHeight() > 0)
        {
            scrolledOnce = true;     // start centered around C4-C5, not C7
            viewport.setViewPosition (0, juce::jmax (0, canvas.pitchToY (78)));
        }
        status.setColour (juce::Label::textColourId, proc.theme().textDim);
        status.setText ("engine: " + proc.renderEngine.getStatus()
                        + "   |   notes: " + juce::String (proc.sequence.countSingableNotes())
                        + "   |   import: play the FL pattern once, or drop a .mid here",
                        juce::dontSendNotification);
    }

    void applyLyrics()
    {
        const auto text = lyricInput.getText();
        if (text.isEmpty()) return;
        const auto sylls = proc.g2p.splitForNotes (text, G2PEngine::guessLanguage (text));
        const auto leftover = proc.sequence.distributeLyrics (sylls);
        proc.renderEngine.requestRender (proc.lastBpm.load());
        status.setText (leftover.isEmpty()
                            ? "lyrics distributed"
                            : juce::String (leftover.size()) + " syllables didn't fit (add notes)",
                        juce::dontSendNotification);
    }

    void translateLyrics()
    {
        translateBtn.setEnabled (false);
        LyricGenerator::UICallbacks ui;
        ui.onConsoleAppend = {};
        ui.onFinished = [this] (bool ok, const juce::String& s)
        {
            translateBtn.setEnabled (true);
            status.setText (ok ? "translated to Japanese + distributed" : s,
                            juce::dontSendNotification);
            if (ok) proc.renderEngine.requestRender (proc.lastBpm.load());
        };
        proc.lyricGen.translateAndApply (lyricInput.getText(), std::move (ui));
    }

    SingScribeProcessor& proc;
    PianoRollCanvas canvas;
    juce::Viewport viewport;
    juce::TextEditor lyricInput;
    juce::TextButton applyBtn { "APPLY LYRICS" }, translateBtn { "TRANSLATE -> JAPANESE" };
    MidiDragOutSpot dragOut;
    juce::Label status;
    juce::Rectangle<int> barArea;
    bool scrolledOnce = false;
};

//==============================================================================
class AILyricistPanel : public juce::Component
{
public:
    explicit AILyricistPanel (SingScribeProcessor& p) : proc (p)
    {
        promptBox.setMultiLine (true);
        promptBox.setReturnKeyStartsNewLine (true);
        promptBox.setTextToShowWhenEmpty ("describe the song: theme, mood, hook idea... (\"10 lines\" works too)",
                                          juce::Colours::grey);
        addAndMakeVisible (promptBox);

        genreMode.addItem ("Traditional Vocaloid", 1);
        genreMode.addItem ("Underground",          2);
        genreMode.setSelectedId (juce::jlimit (1, 2, proc.genreModeId.load()), juce::dontSendNotification);
        genreMode.onChange = [this] { proc.genreModeId.store (genreMode.getSelectedId()); };
        addAndMakeVisible (genreMode);

        generateBtn.onClick = [this] { startGeneration(); };
        addAndMakeVisible (generateBtn);

        console.setMultiLine (true);
        console.setReadOnly (true);
        addAndMakeVisible (console);

        refreshBackendLabel();
        addAndMakeVisible (backendLabel);
    }

    void paint (juce::Graphics& g) override
    {
        g.fillAll (proc.theme().bg1.withAlpha (0.0f));
    }

    void resized() override
    {
        auto r = getLocalBounds().reduced (16);
        promptBox.setBounds (r.removeFromTop (90));
        auto row = r.removeFromTop (44).reduced (0, 7);
        genreMode.setBounds (row.removeFromLeft (220));
        generateBtn.setBounds (row.removeFromLeft (140).withTrimmedLeft (12));
        backendLabel.setBounds (row.withTrimmedLeft (12));
        console.setBounds (r.withTrimmedTop (8));
    }

private:
    void refreshBackendLabel()
    {
        backendLabel.setColour (juce::Label::textColourId, proc.theme().textDim);
        if (auto* b = proc.lyricGen.getBackend())
            backendLabel.setText (b->backendName(), juce::dontSendNotification);
    }

    void startGeneration()
    {
        console.clear();
        refreshBackendLabel();
        const auto mode = genreMode.getSelectedId() == 2
                              ? LyricRequest::Mode::Underground
                              : LyricRequest::Mode::TraditionalVocaloid;

        LyricGenerator::UICallbacks ui;
        ui.onConsoleAppend = [this] (const juce::String& chunk)
        {
            console.moveCaretToEnd();
            console.insertTextAtCaret (chunk);
        };
        ui.onFinished = [this] (bool ok, const juce::String& statusText)
        {
            console.moveCaretToEnd();
            console.insertTextAtCaret ("\n\n[" + statusText + "]\n");
            generateBtn.setEnabled (true);
            if (ok)
                proc.renderEngine.requestRender (proc.lastBpm.load());
        };

        generateBtn.setEnabled (false);
        const auto lang = proc.modelManager.getActiveVoice() != nullptr
                              ? proc.modelManager.getActiveVoice()->language : juce::String ("jpn");
        proc.lyricGen.generateForPattern (promptBox.getText(), mode, lang, std::move (ui));
    }

    SingScribeProcessor& proc;
    juce::TextEditor promptBox, console;
    juce::ComboBox   genreMode;
    juce::TextButton generateBtn { "GENERATE" };
    juce::Label backendLabel;
};

//==============================================================================
class VoicePanel : public juce::Component,
                   private DiffSingerModelManager::Listener
{
public:
    explicit VoicePanel (SingScribeProcessor& p) : proc (p)
    {
        proc.modelManager.addListener (this);

        rescanBtn.onClick = [this] { proc.modelManager.scanForVoices(); };
        openBtn.onClick = [this]
        {
            auto d = DiffSingerModelManager::defaultVoiceDirectory();
            d.createDirectory();
            d.revealToUser();
        };
        voiceBox.onChange = [this]
        {
            const int idx = voiceBox.getSelectedItemIndex();
            if (idx >= 0) proc.modelManager.requestLoad (idx);
        };

        speakerBox.onChange = [this]
        {
            proc.renderEngine.speakerIndex.store (juce::jmax (0, speakerBox.getSelectedItemIndex()));
            proc.renderEngine.requestRender (proc.lastBpm.load());
            previewVoice();
        };

        unisonBox.addItem ("Solo",      1);
        unisonBox.addItem ("Unison x2", 2);
        unisonBox.addItem ("Unison x3", 3);
        unisonBox.setSelectedId (proc.renderEngine.unisonMode.load() + 1, juce::dontSendNotification);
        unisonBox.onChange = [this]
        {
            proc.renderEngine.unisonMode.store (unisonBox.getSelectedId() - 1);
            proc.renderEngine.requestRender (proc.lastBpm.load());
        };

        for (const auto& t : themes())
            themeBox.addItem (t.name, themeBox.getNumItems() + 1);
        themeBox.setSelectedId (proc.themeIndex.load() + 1, juce::dontSendNotification);
        themeBox.onChange = [this]
        {
            proc.themeIndex.store (themeBox.getSelectedId() - 1);
        };

        previewBtn.onClick = [this] { previewVoice(); };

        for (auto* c : std::initializer_list<juce::Component*> {
                 &voiceBox, &rescanBtn, &openBtn, &speakerBox, &previewBtn,
                 &unisonBox, &themeBox, &status, &bridgeToggle, &hint })
            addAndMakeVisible (c);

        bridgeToggle.setToggleState (true, juce::dontSendNotification);
        bridgeToggle.onClick = [this] { proc.g2p.bridge.enabled = bridgeToggle.getToggleState(); };

        hint.setText ("Voices: Music/DiffSinger_Voices/<Name>/  (acoustic + vocoder .onnx, phonemes.txt, .emb speakers). "
                      "Lyric AI: a .gguf in Music/SingScribe_LLM/ (3B instruct recommended).",
                      juce::dontSendNotification);

        refreshList (proc.modelManager.getKnownVoices());
        refreshSpeakers();
    }

    ~VoicePanel() override { proc.modelManager.removeListener (this); }

    void paint (juce::Graphics& g) override
    {
        const auto& th = proc.theme();
        status.setColour (juce::Label::textColourId, th.text.withAlpha (0.85f));
        hint.setColour (juce::Label::textColourId, th.textDim);
        bridgeToggle.setColour (juce::ToggleButton::textColourId, th.text.withAlpha (0.8f));
        bridgeToggle.setColour (juce::ToggleButton::tickColourId, th.accent);

        for (const auto& card : cards)
        {
            g.setColour (th.panel.withAlpha (0.55f));
            g.fillRoundedRectangle (card.toFloat(), 12.0f);
            g.setColour (th.panelLine.withAlpha (0.55f));
            g.drawRoundedRectangle (card.toFloat(), 12.0f, 1.0f);
        }

        auto label = [&] (const char* t, juce::Component& c)
        {
            g.setColour (th.textDim);
            g.setFont (juce::Font (juce::FontOptions (11.0f)));
            g.drawText (t, c.getX(), c.getY() - 16, juce::jmax (160, c.getWidth()), 14,
                        juce::Justification::left);
        };
        label ("VOICEBANK", voiceBox);
        label ("SPEAKER / VOICE COLOR", speakerBox);
        label ("UNISON", unisonBox);
        label ("THEME", themeBox);
    }

    void resized() override
    {
        cards.clear();
        auto r = getLocalBounds().reduced (16);

        auto card1 = r.removeFromTop (76);
        cards.add (card1);
        auto c1 = card1.reduced (14, 0);
        c1.removeFromTop (24);
        auto row = c1.removeFromTop (32);
        voiceBox.setBounds (row.removeFromLeft (juce::jmax (220, row.getWidth() - 260)));
        rescanBtn.setBounds (row.removeFromLeft (96).withTrimmedLeft (10));
        openBtn.setBounds (row.removeFromLeft (130).withTrimmedLeft (10));

        r.removeFromTop (14);
        auto card2 = r.removeFromTop (76);
        cards.add (card2);
        auto c2 = card2.reduced (14, 0);
        c2.removeFromTop (24);
        auto row2 = c2.removeFromTop (32);
        speakerBox.setBounds (row2.removeFromLeft (230));
        previewBtn.setBounds (row2.removeFromLeft (104).withTrimmedLeft (10));
        unisonBox.setBounds (row2.removeFromLeft (126).withTrimmedLeft (16));
        themeBox.setBounds (row2.removeFromLeft (126).withTrimmedLeft (16));

        r.removeFromTop (14);
        status.setBounds (r.removeFromTop (28));
        bridgeToggle.setBounds (r.removeFromTop (28));
        hint.setBounds (r.removeFromTop (52));
    }

private:
    void previewVoice()
    {
        VocalNote n;
        n.midiPitch = 67;
        n.lengthBeats = 1.5;
        n.lyric = "ra";
        proc.renderEngine.requestAudition (n);
    }

    void voicesRescanned (const std::vector<DiffSingerModelManager::VoiceInfo>& v) override
    {
        refreshList (v);
    }
    void voiceLoadStarted (const juce::String& n) override
    {
        status.setText ("loading " + n + "...", juce::dontSendNotification);
    }
    void voiceLoadFinished (const juce::String& n, bool ok, const juce::String& err) override
    {
        status.setText (ok ? n + " ready - click PREVIEW or place a note"
                           : "FAILED: " + err, juce::dontSendNotification);
        refreshSpeakers();
        if (ok)
        {
            proc.renderEngine.requestRender (proc.lastBpm.load());
            previewVoice();
        }
    }

    void refreshSpeakers()
    {
        speakerBox.clear (juce::dontSendNotification);
        if (auto v = proc.modelManager.getActiveVoice())
        {
            int id = 1;
            for (const auto& s : v->speakerNames)
                speakerBox.addItem (s, id++);
            if (v->speakerNames.isEmpty())
                speakerBox.addItem ("(single voice)", 1);
            speakerBox.setSelectedItemIndex (
                juce::jlimit (0, juce::jmax (0, speakerBox.getNumItems() - 1),
                              proc.renderEngine.speakerIndex.load()),
                juce::dontSendNotification);
        }
    }

    void refreshList (const std::vector<DiffSingerModelManager::VoiceInfo>& voices)
    {
        voiceBox.clear (juce::dontSendNotification);
        int id = 1;
        for (const auto& v : voices)
        {
            voiceBox.addItem (v.name + (v.valid ? "" : "  (" + v.error + ")"), id);
            voiceBox.setItemEnabled (id, v.valid);
            ++id;
        }
        if (voices.empty())
            status.setText ("no voices found - click OPEN FOLDER and drop a DiffSinger voicebank in",
                            juce::dontSendNotification);
    }

    SingScribeProcessor& proc;
    juce::ComboBox voiceBox, speakerBox, unisonBox, themeBox;
    juce::TextButton rescanBtn { "RESCAN" }, openBtn { "OPEN FOLDER" }, previewBtn { "PREVIEW" };
    juce::Label status, hint;
    juce::ToggleButton bridgeToggle { "Sing English text on a Japanese voice (transliterate)" };
    juce::Array<juce::Rectangle<int>> cards;
};

//==============================================================================
class SingScribeEditor : public juce::AudioProcessorEditor,
                         public juce::DragAndDropContainer,
                         public juce::FileDragAndDropTarget,
                         private juce::Timer
{
public:
    explicit SingScribeEditor (SingScribeProcessor& p)
        : juce::AudioProcessorEditor (p), proc (p),
          tabs (juce::TabbedButtonBar::TabsAtTop),
          voicePanel (p), rollTab (p), lyricist (p)
    {
        applyTheme();
        tabs.addTab ("VOICE",       juce::Colours::transparentBlack, &voicePanel, false);
        tabs.addTab ("PIANO ROLL",  juce::Colours::transparentBlack, &rollTab,    false);
        tabs.addTab ("AI LYRICIST", juce::Colours::transparentBlack, &lyricist,   false);
        tabs.setCurrentTabIndex (1);
        tabs.setOutline (0);
        addAndMakeVisible (tabs);
        setSize (1000, 620);
        setResizable (true, true);
        setResizeLimits (780, 460, 2400, 1600);
        startTimerHz (4);
    }

    ~SingScribeEditor() override { setLookAndFeel (nullptr); }

    void resized() override
    {
        tabs.setBounds (getLocalBounds().withTrimmedTop (42));
    }

    void paint (juce::Graphics& g) override
    {
        const auto& th = proc.theme();
        g.setGradientFill (juce::ColourGradient (th.bg1, 0, 0, th.bg2,
                                                 (float) getWidth(), (float) getHeight(), false));
        g.fillAll();

        // brand header
        g.setColour (th.accent);
        g.fillEllipse (18.0f, 14.0f, 14.0f, 14.0f);
        g.setColour (th.accent2.withAlpha (0.7f));
        g.fillEllipse (26.0f, 14.0f, 14.0f, 14.0f);
        g.setColour (th.text);
        g.setFont (juce::Font (juce::FontOptions (juce::Font::getDefaultSansSerifFontName(),
                                                  20.0f, juce::Font::bold)));
        g.drawText ("EX2-VOICE", 50, 8, 220, 26, juce::Justification::centredLeft);
        g.setColour (th.textDim);
        g.setFont (juce::Font (juce::FontOptions (12.0f)));
        g.drawText ("AI vocal synth  -  v1.3", getWidth() - 220, 12, 200, 18,
                    juce::Justification::centredRight);
        g.setColour (th.panelLine.withAlpha (0.7f));
        g.fillRect (0, 41, getWidth(), 1);
    }

    bool isInterestedInFileDrag (const juce::StringArray& files) override
    {
        for (const auto& f : files)
            if (f.endsWithIgnoreCase (".mid")) return true;
        return false;
    }

    void filesDropped (const juce::StringArray& files, int, int) override
    {
        for (const auto& path : files)
        {
            juce::File f (path);
            if (f.hasFileExtension ("mid"))
                if (importMidiIntoSequence (f, proc.sequence) > 0)
                    proc.renderEngine.requestRender (proc.lastBpm.load());
        }
    }

private:
    void timerCallback() override
    {
        if (proc.themeIndex.load() != appliedTheme)
            applyTheme();
    }

    void applyTheme()
    {
        appliedTheme = proc.themeIndex.load();
        const auto& th = themeAt (appliedTheme);
        lnf.th = &th;

        lnf.setColourScheme (th.light ? juce::LookAndFeel_V4::getLightColourScheme()
                                      : juce::LookAndFeel_V4::getDarkColourScheme());
        lnf.setColour (juce::ComboBox::backgroundColourId, th.panel);
        lnf.setColour (juce::ComboBox::textColourId, th.text);
        lnf.setColour (juce::ComboBox::outlineColourId, th.panelLine);
        lnf.setColour (juce::ComboBox::arrowColourId, th.accent);
        lnf.setColour (juce::PopupMenu::backgroundColourId, th.panel);
        lnf.setColour (juce::PopupMenu::textColourId, th.text);
        lnf.setColour (juce::PopupMenu::highlightedBackgroundColourId, th.accent.withAlpha (0.35f));
        lnf.setColour (juce::PopupMenu::highlightedTextColourId, th.text);
        lnf.setColour (juce::TextButton::buttonColourId, th.panel);
        lnf.setColour (juce::TextButton::textColourOffId, th.text);
        lnf.setColour (juce::TextButton::textColourOnId, th.light ? juce::Colours::white : juce::Colours::black);
        lnf.setColour (juce::TextButton::buttonOnColourId, th.accent);
        lnf.setColour (juce::TextEditor::backgroundColourId, th.panel);
        lnf.setColour (juce::TextEditor::textColourId, th.text);
        lnf.setColour (juce::TextEditor::outlineColourId, th.panelLine);
        lnf.setColour (juce::TextEditor::focusedOutlineColourId, th.accent);
        lnf.setColour (juce::CaretComponent::caretColourId, th.accent);
        lnf.setColour (juce::Label::textColourId, th.text);
        lnf.setColour (juce::TabbedButtonBar::tabTextColourId, th.textDim);
        lnf.setColour (juce::TabbedButtonBar::frontTextColourId, th.accent);
        lnf.setColour (juce::TabbedComponent::backgroundColourId, juce::Colours::transparentBlack);
        lnf.setColour (juce::TabbedComponent::outlineColourId, juce::Colours::transparentBlack);
        lnf.setColour (juce::ScrollBar::thumbColourId, th.accent.withAlpha (0.5f));

        setLookAndFeel (&lnf);
        sendLookAndFeelChange();
        repaint();
    }

    SingScribeProcessor& proc;
    SSLookAndFeel lnf;
    int appliedTheme = -1;
    juce::TabbedComponent tabs;
    VoicePanel voicePanel;
    RollTab rollTab;
    AILyricistPanel lyricist;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (SingScribeEditor)
};

inline juce::AudioProcessorEditor* SingScribeProcessor::createEditor()
{
    return new SingScribeEditor (*this);
}

} // namespace ss
