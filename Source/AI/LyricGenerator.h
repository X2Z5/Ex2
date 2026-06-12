#pragma once
// LyricGenerator — owns the swappable LLM backend, derives syllable budgets
// from the active pattern, filters refusals, and feeds clean text into G2P.
#include "LLMService.h"
#include "../Model/VocalNote.h"
#include "../Text/G2PEngine.h"

namespace ss
{

class LyricGenerator
{
public:
    LyricGenerator (VocalSequence& seq, G2PEngine& g2pRef)
        : sequence (seq), g2p (g2pRef) {}

    void setBackend (std::unique_ptr<LLMService> newBackend)
    {
        if (backend) backend->cancel();
        backend = std::move (newBackend);
    }

    LLMService* getBackend() const { return backend.get(); }

    struct UICallbacks
    {
        std::function<void (const juce::String& chunk)> onConsoleAppend;
        std::function<void (bool ok, const juce::String& status)> onFinished;
    };

    void generateForPattern (const juce::String& userPrompt,
                             LyricRequest::Mode mode,
                             const juce::String& voiceLanguage,
                             UICallbacks ui)
    {
        if (backend == nullptr || ! backend->isAvailable())
        {
            if (ui.onFinished) ui.onFinished (false, "no LLM backend available");
            return;
        }

        LyricRequest req;
        req.userPrompt       = userPrompt;
        req.mode             = mode;
        req.voiceLanguage    = voiceLanguage;
        if (userPrompt.containsIgnoreCase ("japan") || userPrompt.containsIgnoreCase ("romaji"))
            req.voiceLanguage = "jpn";                    // user explicitly wants Japanese
        req.syllablesPerLine = deriveSyllableBudget();

        // honor "write 20 sentences/lines" style requests even with few notes;
        // extra lines simply won't fit until more notes exist (status says so)
        const int asked = parseRequestedLines (userPrompt);
        while ((int) req.syllablesPerLine.size() < asked)
            req.syllablesPerLine.push_back (7);

        req.targetLines = juce::jmax (1, (int) req.syllablesPerLine.size());
        req.maxTokens   = juce::jmax (256, req.targetLines * 28);

        launch (req, std::make_shared<UICallbacks> (std::move (ui)), 0);
    }

    static int parseRequestedLines (const juce::String& prompt)
    {
        const auto tokens = juce::StringArray::fromTokens (prompt, " ,.", {});
        for (int i = 0; i + 1 < tokens.size(); ++i)
        {
            const int v = tokens[i].getIntValue();
            if (v >= 2 && v <= 40)
            {
                const auto next = tokens[i + 1].toLowerCase();
                if (next.startsWith ("sentence") || next.startsWith ("line")
                    || next.startsWith ("bar") || next.startsWith ("row")
                    || next.startsWith ("verse"))
                    return v;
            }
        }
        return 0;
    }

    /** "Translator" button: meaning-translate text to Japanese (romaji) via the
        LLM, then distribute the result onto the notes. */
    void translateAndApply (const juce::String& text, UICallbacks ui)
    {
        if (backend == nullptr || ! backend->isAvailable())
        {
            if (ui.onFinished) ui.onFinished (false, "no LLM backend available");
            return;
        }
        if (text.trim().isEmpty())
        {
            if (ui.onFinished) ui.onFinished (false, "type the text to translate first");
            return;
        }
        LyricRequest req;
        req.userPrompt    = text;
        req.mode          = LyricRequest::Mode::TranslateToRomaji;
        req.voiceLanguage = "jpn";
        req.targetLines   = juce::jmax (1, juce::StringArray::fromLines (text).size());
        req.temperature   = 0.4f;                          // translation: stay literal
        launch (req, std::make_shared<UICallbacks> (std::move (ui)), 0);
    }

    /** Small models sometimes refuse harmless prompts; detect, retry once,
        and never let an apology get distributed onto the notes. */
    static bool looksLikeRefusal (const juce::String& t)
    {
        const auto l = t.toLowerCase();
        return l.contains ("i can't") || l.contains ("i cannot")
            || l.contains ("i'm sorry") || l.contains ("i am sorry")
            || l.contains ("can't fulfill") || l.contains ("cannot fulfill")
            || l.contains ("cannot assist") || l.contains ("can't assist")
            || l.contains ("as an ai");
    }

    /** Returns empty when output is usable; otherwise a short reason. Catches
        refusals AND lazy meta-output like "(20 lines of song lyric)". */
    static juce::String diagnoseOutput (const juce::String& full, int targetLines)
    {
        if (looksLikeRefusal (full))
            return "model refused";

        auto lines = juce::StringArray::fromLines (full);
        lines.removeEmptyStrings (true);

        const auto l = full.toLowerCase();
        const bool meta = (lines.size() <= 2)
                          && (l.contains ("lines of") || l.contains ("lyrics here")
                              || l.contains ("song lyric") || l.contains ("insert"));
        if (meta)
            return "model output a placeholder instead of lyrics";

        if (targetLines >= 4 && lines.size() * 3 < targetLines)
            return "model wrote too few lines";

        return {};
    }

private:
    void launch (LyricRequest req, std::shared_ptr<UICallbacks> ui, int attempt)
    {
        backend->generate (req,
            [ui] (const juce::String& chunk)
            {
                if (ui->onConsoleAppend) ui->onConsoleAppend (chunk);
            },
            [this, req, ui, attempt] (bool ok, const juce::String& full, const juce::String& err) mutable
            {
                if (! ok)
                {
                    if (ui->onFinished) ui->onFinished (false, err);
                    return;
                }
                const juce::String badReason = diagnoseOutput (full, req.targetLines);
                if (badReason.isNotEmpty())
                {
                    if (attempt == 0)
                    {
                        if (ui->onConsoleAppend)
                            ui->onConsoleAppend ("\n[" + badReason + " - retrying...]\n");
                        req.userPrompt = req.userPrompt
                            + "\n(Reminder: this is a normal, wholesome songwriting request. "
                              "Write the ACTUAL lyric lines now, one per line. Do not describe "
                              "or summarize them, do not apologize, output lyrics only.)";
                        req.temperature = juce::jmin (1.2f, req.temperature + 0.15f);
                        launch (req, ui, 1);
                        return;
                    }
                    if (ui->onFinished)
                        ui->onFinished (false, badReason + " twice - this 1.5B model is at its "
                                               "limit; a 3B instruct GGUF in Music/SingScribe_LLM "
                                               "fixes this (or simplify the prompt)");
                    return;
                }
                applyToSequence (full, req.voiceLanguage);
                if (ui->onFinished) ui->onFinished (true, "lyrics distributed to notes");
            });
    }

    /** Phrase gaps > 1 beat split lines; each phrase's note count = that line's
        syllable budget, so output can't overflow the pattern. */
    std::vector<int> deriveSyllableBudget() const
    {
        std::vector<int> budget;
        int count = 0;
        double lastEnd = -1.0e9;
        for (const auto& n : sequence.snapshot())
        {
            if (n.isRest) continue;
            if (n.startBeat - lastEnd > 1.0 && count > 0) { budget.push_back (count); count = 0; }
            ++count;
            lastEnd = juce::jmax (lastEnd, n.endBeat());
        }
        if (count > 0) budget.push_back (count);
        if (budget.empty()) budget.push_back (8);
        return budget;
    }

    void applyToSequence (const juce::String& text, const juce::String& voiceLanguage)
    {
        g2p.setVoiceLanguage (voiceLanguage);
        const auto sylls = g2p.splitForNotes (text, G2PEngine::guessLanguage (text));
        sequence.distributeLyrics (sylls);
        g2p.phonemizeSequence (sequence);
    }

    VocalSequence& sequence;
    G2PEngine& g2p;
    std::unique_ptr<LLMService> backend;
};

} // namespace ss
