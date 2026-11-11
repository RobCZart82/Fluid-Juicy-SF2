#include "SoundFontFolders.h"

namespace {
// POSIX file locks coordinate processes, so also serialize instances in this host.
juce::CriticalSection processSettingsLock;
}

juce::File SoundFontFolders::defaultSettingsFile()
{
    auto root = juce::File::getSpecialLocation(juce::File::userApplicationDataDirectory);
#if JUCE_MAC
    root = root.getChildFile("Application Support");
#endif
    return root.getChildFile("Fluid Juicy SF2/SoundFontFolders.xml");
}

SoundFontFolders::SoundFontFolders(juce::File settings)
    : settingsFile(std::move(settings)),
      lockName("FluidJuicySF2Folders" + juce::String(settingsFile.getFullPathName().hashCode64())) {}

SoundFontFolders::Snapshot SoundFontFolders::readUnlocked() const
{
    Snapshot saved;
    if (!settingsFile.exists()) return saved;
    if (settingsFile.getSize() > 1024 * 1024) {
        saved.error = "SoundFont folder settings are too large.";
        return saved;
    }
    auto xml = juce::XmlDocument::parse(settingsFile);
    if (!xml || !xml->hasTagName("SoundFontFolders") || xml->getIntAttribute("version") != 1) {
        saved.error = "Could not read SoundFont folder settings: " + settingsFile.getFullPathName();
        return saved;
    }
    for (auto* child : xml->getChildIterator()) {
        const auto path = child->getStringAttribute("path");
        const auto encoded = child->getStringAttribute("bookmark");
        const auto bytes = encoded.upToFirstOccurrenceOf(".", false, false).getLargeIntValue();
        juce::MemoryBlock bookmark;
        if (!child->hasTagName("Folder") || !juce::File::isAbsolutePath(path)
            || bytes < 0 || bytes > 16384 || !bookmark.fromBase64Encoding(encoded)
            || saved.folders.size() >= 16) {
            saved.error = "Invalid SoundFont folder settings: " + settingsFile.getFullPathName();
            saved.folders.clear();
            return saved;
        }
        saved.folders.add({juce::File(path), std::move(bookmark)});
    }
    return saved;
}

SoundFontFolders::Snapshot SoundFontFolders::read() const
{
    const juce::ScopedLock processGuard(processSettingsLock);
    juce::InterProcessLock lock(lockName);
    const juce::InterProcessLock::ScopedLockType guard(lock);
    if (!guard.isLocked()) return {{}, "Could not lock SoundFont folder settings."};
    return readUnlocked();
}

juce::Result SoundFontFolders::add(const juce::File& path, const juce::MemoryBlock& bookmark)
{
    if (!path.isDirectory()) return juce::Result::fail("Choose an existing SoundFont folder.");
    if (bookmark.getSize() > 16384) return juce::Result::fail("The folder access bookmark is too large.");
    return change({path, bookmark}, false);
}

juce::Result SoundFontFolders::remove(const juce::File& path) { return change({path, {}}, true); }

juce::Result SoundFontFolders::change(const Folder& target, bool remove)
{
    const juce::ScopedLock processGuard(processSettingsLock);
    juce::InterProcessLock lock(lockName);
    const juce::InterProcessLock::ScopedLockType guard(lock);
    if (!guard.isLocked()) return juce::Result::fail("Could not lock SoundFont folder settings.");
    auto saved = readUnlocked();
    if (saved.error.isNotEmpty()) return juce::Result::fail(saved.error);
    for (int i = saved.folders.size(); --i >= 0;)
        if (saved.folders[i].path == target.path) saved.folders.remove(i);
    if (!remove) {
        if (saved.folders.size() >= 16) return juce::Result::fail("Up to 16 additional SoundFont folders can be remembered.");
        saved.folders.add(target);
    }
    juce::XmlElement xml("SoundFontFolders");
    xml.setAttribute("version", 1);
    for (const auto& folder : saved.folders) {
        auto* element = xml.createNewChildElement("Folder");
        element->setAttribute("path", folder.path.getFullPathName());
        element->setAttribute("bookmark", folder.bookmark.toBase64Encoding());
    }
    if (xml.toString().getNumBytesAsUTF8() > 1024 * 1024)
        return juce::Result::fail("SoundFont folder settings are too large.");
    if (const auto created = settingsFile.getParentDirectory().createDirectory(); created.failed()) return created;
    juce::TemporaryFile temporary(settingsFile);
    if (!xml.writeTo(temporary.getFile()) || !temporary.overwriteTargetFileWithTemporary())
        return juce::Result::fail("Could not save SoundFont folders: " + settingsFile.getFullPathName());
    return juce::Result::ok();
}
