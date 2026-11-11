//
// Created by Alex Birch on 03/10/2017.
//

#include "FilePicker.h"
#include "Util.h"

// #ifdef __APPLE__
//   #include <CoreFoundation/CFURL.h>
// #endif
#include "MacFileAccess.h"
#if JUCE_MAC || JUCE_IOS
using JuicyMac::CFPtr;
#endif

FilePicker::FilePicker(
    AudioProcessorValueTreeState& valueTreeState,
    FluidSynthModel& model
)
: fileChooser{
    "File",
    File(),
    true,
    false,
    false,
    "*.sf2;*.sf3",
    String(),
    "Choose a Soundfont file to load into the synthesizer"}
, fluidSynthModel{model}
, valueTreeState{valueTreeState}
// , fluidSynthModel{fluidSynthModel}
// , currentPath{}
#if JUCE_MAC || JUCE_IOS
, bookmarkCreationOptions{kCFURLBookmarkCreationWithSecurityScope}
#endif
{
    // faster (rounded edges introduce transparency)
    setOpaque (true);

    // setDisplayedFilePath(fluidSynthModel.getCurrentSoundFontAbsPath());
    setDisplayedFilePath(valueTreeState.state.getChildWithName("soundFont").getProperty("path", ""));

    addAndMakeVisible (fileChooser);
    addAndMakeVisible(libraryButton);
    libraryButton.setTooltip("Find SF2/SF3 files in the user and shared SoundFont folders");
    libraryButton.onClick = [this] { showLibrary(); };
    fileChooser.addListener (this);
    valueTreeState.state.addListener(this);
    triggerAsyncUpdate();
//    valueTreeState.state.getChildWithName("soundFont").sendPropertyChangeMessage("path");

#if JUCE_MAC || JUCE_IOS
    bookmarkCreationOptions |= kCFURLBookmarkCreationSecurityScopeAllowOnlyReadAccess;
#endif
}
FilePicker::~FilePicker() {
    library.cancel();
    cancelPendingUpdate();
    fileChooser.removeListener (this);
    valueTreeState.state.removeListener(this);
}

void FilePicker::resized() {
    Rectangle<int> r (getLocalBounds());
    libraryButton.setBounds(r.removeFromRight(60));
    r.removeFromRight(4);
    fileChooser.setBounds (r);
}

void FilePicker::showLibrary() {
    libraryButton.setEnabled(false);
    libraryButton.setButtonText("Scanning");
    library.scan([this](SoundFontLibrary::Result result) {
        libraryButton.setEnabled(true);
        libraryButton.setButtonText("Banks");
        juce::PopupMenu menu;
        if (result.files.isEmpty()) menu.addItem("No SF2/SF3 files found", false, false, nullptr);
        int id = 1;
        for (const auto& file : result.files)
            menu.addItem(id++, file.getFileName() + " - " + file.getParentDirectory().getFullPathName());
        if (result.limitReached) menu.addItem("Scan limit reached; use Browse for other files", false, false, nullptr);
        menu.addSeparator();
        menu.addItem(1001, "Refresh");
        const auto folders = SoundFontLibrary::defaultFolders();
        menu.addSectionHeader("SoundFont folders");
        for (const auto& folder : folders)
            menu.addItem(folder.getFullPathName(), false, false, nullptr);
        juce::Component::SafePointer<FilePicker> safeThis(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&libraryButton),
            [safeThis, files = std::move(result.files)](int choice) {
                if (!safeThis) return;
                if (choice == 1001) safeThis->showLibrary();
                else if (choice > 0 && choice <= files.size()) {
                    safeThis->fileChooser.setCurrentFile(files[choice - 1], true, dontSendNotification);
                    safeThis->filenameComponentChanged(nullptr);
                }
            });
    });
}

/**
 * This is required to support setOpaque(true)
 */
void FilePicker::paint(Graphics& g)
{
    g.fillAll(findColour(ResizableWindow::backgroundColourId));
}

void FilePicker::filenameComponentChanged (FilenameComponent*) {
    MemoryBlock bookmark;
#if JUCE_MAC || JUCE_IOS
    CFPtr<CFStringRef> filePath{fileChooser.getCurrentFile().getFullPathName().toCFString()};
    CFPtr<CFURLRef> url{CFURLCreateWithFileSystemPath(nullptr, filePath.get(), kCFURLPOSIXPathStyle, false)};
    CFErrorRef error = nullptr;
    CFPtr<CFDataRef> data{url ? CFURLCreateBookmarkData(nullptr, url.get(),
        bookmarkCreationOptions, nullptr, nullptr, &error) : nullptr};
    CFPtr<CFErrorRef> ownedError{error};
    if (data) {
        bookmark.append(CFDataGetBytePtr(data.get()), static_cast<size_t>(CFDataGetLength(data.get())));
    }

#endif
    fluidSynthModel.requestSoundFont(fileChooser.getCurrentFile().getFullPathName(), bookmark);
    // Also refresh after repeated failures whose identical error property may not notify.
    triggerAsyncUpdate();
}

void FilePicker::valueTreePropertyChanged(ValueTree& treeWhosePropertyHasChanged,
                                               const Identifier& property) {
    if (treeWhosePropertyHasChanged.getType() == StringRef("soundFont")
        && (property == StringRef("path") || property == StringRef("loadError") || property == StringRef("loading")))
        triggerAsyncUpdate();
}

void FilePicker::handleAsyncUpdate() {
    const auto state = valueTreeState.state.getChildWithName("soundFont");
    const String path = state.getProperty("path", "");
    // Empty project state also clears the previous displayed bank.
    fileChooser.setCurrentFile(path.isEmpty() ? File() : File(path), true, dontSendNotification);
    currentPath = path;
    const String error = state.getProperty("loadError", "");
    fileChooser.setTooltip(fluidSynthModel.isLoading() ? "Loading SoundFont in the background..."
        : error.isEmpty() ? "Choose an SF2 or SF3 SoundFont" : error);
    if (error.isNotEmpty() && error != displayedError)
        juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
            "SoundFont could not be loaded", error);
    displayedError = error;
}

void FilePicker::setDisplayedFilePath(const String& path) {
     if (!shouldChangeDisplayedFilePath(path)) {
         return;
     }
    currentPath = path;
    fileChooser.setCurrentFile(File(path), true, dontSendNotification);
}

bool FilePicker::shouldChangeDisplayedFilePath(const String &path) {
    if (path.isEmpty()) {
        return false;
    }
    if (path == currentPath) {
        return false;
    }
    return true;
}
