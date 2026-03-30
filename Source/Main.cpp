#include <JuceHeader.h>
#include "MainComponent.h"

//==============================================================================
class Surround51Application : public juce::JUCEApplication
{
public:
    // Default constructor keeps JUCEApplication in a ready state.
    Surround51Application() {}

    // Returns the app name shown in window title and OS process listing.
    const juce::String getApplicationName() override
    {
        return JUCE_APPLICATION_NAME_STRING;
    }

    // Returns app version string from project metadata.
    const juce::String getApplicationVersion() override
    {
        return JUCE_APPLICATION_VERSION_STRING;
    }

    // Multiple instances are allowed for testing/comparison workflows.
    bool moreThanOneInstanceAllowed() override { return true; }

    // Creates the main top-level window during startup.
    void initialise(const juce::String&) override
    {
        mainWindow.reset(new MainWindow(getApplicationName()));
    }

    // Releases the window on shutdown.
    void shutdown() override
    {
        mainWindow = nullptr;
    }

    // Called by OS close request; forwards to JUCE quit flow.
    void systemRequestedQuit() override
    {
        quit();
    }

    //==============================================================================
    class MainWindow : public juce::DocumentWindow
    {
    public:
        // Configures resizable native window and hosts MainComponent content.
        MainWindow(juce::String name)
            : DocumentWindow(name,
                             juce::Colour(0xff07090c),
                             DocumentWindow::allButtons)
        {
            setUsingNativeTitleBar(false);
            // Keep the window size independent from content size.
            setContentOwned(new MainComponent(), false);
            setResizable(true, true);

            auto userArea = juce::Rectangle<int>(0, 0, 1366, 768);
            if (auto* display = juce::Desktop::getInstance().getDisplays().getPrimaryDisplay())
                userArea = display->userArea;

            const int maxW = juce::jmax(640, userArea.getWidth());
            const int maxH = juce::jmax(520, userArea.getHeight());
            const int minW = juce::jmin(980, maxW);
            const int minH = juce::jmin(700, maxH);
            const int startW = juce::jmax(minW, juce::jmin(800, maxW));
            // Open using full vertical space of the current monitor work area.
            const int startH = maxH;

            setResizeLimits(minW, minH, maxW, maxH);
            setBounds(userArea.withSizeKeepingCentre(startW, startH));
            setVisible(true);
        }

        // Handles title-bar close button.
        void closeButtonPressed() override
        {
            JUCEApplication::getInstance()->systemRequestedQuit();
        }

    private:
        JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(MainWindow)
    };

private:
    // Owning pointer to the main app window.
    std::unique_ptr<MainWindow> mainWindow;
};

START_JUCE_APPLICATION(Surround51Application)
