//
// Created by Alex Birch on 03/10/2017.
//

#include "FilePicker.h"
#include "Util.h"
#include "SoundFontFolders.h"

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
{
    // faster (rounded edges introduce transparency)
    setOpaque (true);

    // setDisplayedFilePath(fluidSynthModel.getCurrentSoundFontAbsPath());
    setDisplayedFilePath(valueTreeState.state.getChildWithName("soundFont").getProperty("path", ""));

    addAndMakeVisible (fileChooser);
    addAndMakeVisible(libraryButton);
    addChildComponent(locateButton);
    addChildComponent(statusLabel);
    locateButton.setComponentID("locateSoundFont");
    statusLabel.setComponentID("soundFontStatus");
    locateButton.setTooltip("Locate the SoundFont used by this project at its new location");
    locateButton.onClick = [this] { locateSoundFont(); };
    statusLabel.setFont(juce::FontOptions(12.0f));
    libraryButton.setTooltip("Find SF2/SF3 files in the user and shared SoundFont folders");
    libraryButton.onClick = [this] { showLibrary(); };
    fileChooser.addListener (this);
    valueTreeState.state.addListener(this);
    handleAsyncUpdate();
//    valueTreeState.state.getChildWithName("soundFont").sendPropertyChangeMessage("path");

}
FilePicker::~FilePicker() {
    relocationChooser.reset();
    folderChooser.reset();
    library.cancel();
    cancelPendingUpdate();
    fileChooser.removeListener (this);
    valueTreeState.state.removeListener(this);
}

void FilePicker::resized() {
    Rectangle<int> bounds (getLocalBounds());
    Rectangle<int> r = bounds.removeFromTop(25);
    statusLabel.setBounds(bounds);
    if (locateButton.isVisible()) {
        locateButton.setBounds(r.removeFromRight(72));
        r.removeFromRight(4);
    }
    libraryButton.setBounds(r.removeFromRight(60));
    r.removeFromRight(4);
    fileChooser.setBounds (r);
}

void FilePicker::showLibrary(SoundFontLibrary::Edit edit) {
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
        if (result.error.isNotEmpty()) menu.addItem(result.error, false, false, nullptr);
        menu.addSeparator();
        menu.addItem(1001, "Refresh");
        menu.addItem(1002, "Add folder...");
        juce::PopupMenu remove;
        for (int i = 0; i < result.savedFolders.size(); ++i)
            remove.addItem(2001 + i, result.savedFolders[i].getFullPathName());
        if (!result.savedFolders.isEmpty()) menu.addSubMenu("Remove folder", remove);
        const auto folders = SoundFontLibrary::defaultFolders();
        menu.addSectionHeader("SoundFont folders");
        for (const auto& folder : folders)
            menu.addItem(folder.getFullPathName(), false, false, nullptr);
        juce::Component::SafePointer<FilePicker> safeThis(this);
        menu.showMenuAsync(juce::PopupMenu::Options().withTargetComponent(&libraryButton),
            [safeThis, result = std::move(result)](int choice) {
                if (!safeThis) return;
                if (choice == 1001) safeThis->showLibrary();
                else if (choice == 1002) safeThis->addLibraryFolder();
                else if (choice >= 2001 && choice < 2001 + result.savedFolders.size()) {
                    const auto folder = result.savedFolders[choice - 2001];
                    safeThis->showLibrary([folder](const std::function<bool()>& cancelled) {
                        return SoundFontFolders().remove(folder, cancelled);
                    });
                } else if (choice > 0 && choice <= result.files.size()) {
                    safeThis->fileChooser.setCurrentFile(result.files[choice - 1], true, dontSendNotification);
                    safeThis->filenameComponentChanged(nullptr);
                }
            });
    }, std::move(edit));
}

void FilePicker::addLibraryFolder() {
    folderChooser = std::make_unique<juce::FileChooser>("Choose a SoundFont folder");
    juce::Component::SafePointer<FilePicker> safeThis(this);
    folderChooser->launchAsync(juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectDirectories,
        [safeThis](const juce::FileChooser& chooser) {
            if (!safeThis) return;
            const auto folder = chooser.getResult();
            if (!folder.isDirectory()) return;
            MemoryBlock bookmark;
#if JUCE_MAC || JUCE_IOS
            bookmark = JuicyMac::createReadOnlyBookmark(folder);
            if (bookmark.getSize() == 0) {
                juce::AlertWindow::showMessageBoxAsync(juce::AlertWindow::WarningIcon,
                    "SoundFont folders", "Could not save access to this folder. Use Browse to select a bank directly.");
                return;
            }
#endif
            safeThis->showLibrary([folder, bookmark](const std::function<bool()>& cancelled) {
                return SoundFontFolders().add(folder, bookmark, cancelled);
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

void FilePicker::locateSoundFont() {
    // No automatic filename search: the user chooses the replacement bank.
    const auto saved = fluidSynthModel.getSoundFontState();
    relocationChooser = std::make_unique<juce::FileChooser>(
        "Locate SoundFont: " + File(saved.path).getFileName(), File(saved.path), "*.sf2;*.sf3");
    juce::Component::SafePointer<FilePicker> safeThis(this);
    relocationChooser->launchAsync(juce::FileBrowserComponent::openMode
        | juce::FileBrowserComponent::canSelectFiles,
        [safeThis](const juce::FileChooser& chooser) {
            if (!safeThis) return;
            const auto file = chooser.getResult();
            if (file == File()) return; // Cancellation changes neither state nor parameters.
            safeThis->loadSelectedFile(file);
        });
}

void FilePicker::loadSelectedFile(const File& file) {
    MemoryBlock bookmark;
#if JUCE_MAC || JUCE_IOS
    bookmark = JuicyMac::createReadOnlyBookmark(file);
#endif
    fluidSynthModel.requestSoundFont(file.getFullPathName(), bookmark);
    triggerAsyncUpdate();
}

void FilePicker::filenameComponentChanged (FilenameComponent*) {
    loadSelectedFile(fileChooser.getCurrentFile());
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
    const bool loading = fluidSynthModel.isLoading();
    const bool unavailable = error.isNotEmpty() && !fluidSynthModel.hasLoadedSoundFont() && path.isNotEmpty();
    const int previousHeight = getPreferredHeight();
    locateButton.setVisible(unavailable);
    locateButton.setEnabled(!loading);
    statusLabel.setVisible(loading || unavailable);
    statusLabel.setText(loading ? "Loading SoundFont..." : "SoundFont unavailable. Locate the file or choose another bank.",
                        dontSendNotification);
    statusLabel.setTooltip(error);
    fileChooser.setTooltip(loading ? "Loading SoundFont in the background..."
        : error.isEmpty() ? "Choose an SF2 or SF3 SoundFont" : error);
    resized();
    if (previousHeight != getPreferredHeight() && onLayoutChanged) onLayoutChanged();
    if (!unavailable && error.isNotEmpty() && error != displayedError)
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
