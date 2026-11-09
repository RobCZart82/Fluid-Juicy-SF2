//
// Created by Alex Birch on 03/10/2017.
//

#include "FilePicker.h"
#include "MyColours.h"
#include "Util.h"

// #ifdef __APPLE__
//   #include <CoreFoundation/CFURL.h>
// #endif
#include "MacFileAccess.h"
#if JUCE_MAC || JUCE_IOS
using JuicyMac::CFPtr;
#endif

FilePicker::FilePicker(
    AudioProcessorValueTreeState& valueTreeState
    // FluidSynthModel& fluidSynthModel
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
    fileChooser.addListener (this);
    valueTreeState.state.addListener(this);
//    valueTreeState.state.getChildWithName("soundFont").sendPropertyChangeMessage("path");

#if JUCE_MAC || JUCE_IOS
    bookmarkCreationOptions |= kCFURLBookmarkCreationSecurityScopeAllowOnlyReadAccess;
#endif
}
FilePicker::~FilePicker() {
    fileChooser.removeListener (this);
    valueTreeState.state.removeListener(this);
}

void FilePicker::resized() {
    Rectangle<int> r (getLocalBounds());
    fileChooser.setBounds (r);
}

/**
 * This is required to support setOpaque(true)
 */
void FilePicker::paint(Graphics& g)
{
    g.fillAll(MyColours::getUIColourIfAvailable(LookAndFeel_V4::ColourScheme::UIColour::windowBackground, juce::Colours::lightgrey));
}

void FilePicker::filenameComponentChanged (FilenameComponent*) {
#if JUCE_MAC || JUCE_IOS
    CFPtr<CFStringRef> filePath{fileChooser.getCurrentFile().getFullPathName().toCFString()};
    CFPtr<CFURLRef> url{CFURLCreateWithFileSystemPath(nullptr, filePath.get(), kCFURLPOSIXPathStyle, false)};
    CFErrorRef error = nullptr;
    CFPtr<CFDataRef> data{url ? CFURLCreateBookmarkData(nullptr, url.get(),
        bookmarkCreationOptions, nullptr, nullptr, &error) : nullptr};
    CFPtr<CFErrorRef> ownedError{error};
    MemoryBlock bookmark;
    if (data) {
        bookmark.append(CFDataGetBytePtr(data.get()), static_cast<size_t>(CFDataGetLength(data.get())));
    }
    // Empty bookmarks are valid: non-sandboxed hosts can use the saved path.
    Value bookmarkValue{valueTreeState.state.getChildWithName("soundFont").getPropertyAsValue("bookmark", nullptr)};
    bookmarkValue.setValue(var(bookmark));
#endif
    // currentPath = fileChooser.getCurrentFile().getFullPathName();
    // fluidSynthModel.onFileNameChanged(fileChooser.getCurrentFile().getFullPathName(), -1, -1);
    Value value{valueTreeState.state.getChildWithName("soundFont").getPropertyAsValue("path", nullptr)};
    value.setValue(fileChooser.getCurrentFile().getFullPathName());
//    value = fileChooser.getCurrentFile().getFullPathName();
}

void FilePicker::valueTreePropertyChanged(ValueTree& treeWhosePropertyHasChanged,
                                               const Identifier& property) {
    if (treeWhosePropertyHasChanged.getType() == StringRef("soundFont")) {
    // if (&treeWhosePropertyHasChanged == &valueTree) {
        if (property == StringRef("path")) {
            String soundFontPath = treeWhosePropertyHasChanged.getProperty("path", "");
            DEBUG_PRINT(soundFontPath);
            setDisplayedFilePath(soundFontPath);
            // if (soundFontPath.isNotEmpty()) {
            //     loadFont(soundFontPath);
            // }
        }
    }
}

void FilePicker::setDisplayedFilePath(const String& path) {
     if (!shouldChangeDisplayedFilePath(path)) {
         return;
     }
    // currentPath = path;
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
