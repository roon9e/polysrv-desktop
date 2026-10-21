#!/usr/bin/env python3
"""
This file is part of PolySRV Desktop,
a Telegram Desktop fork for the PolySRV multi-server client.

For license and copyright information please follow this link:
https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL

Applies the branding to the tree.

Telegram/build/branding holds the branding values and is the single source of
truth. This script is the only thing that writes them into the sources, so
renaming the product means editing that one file and running this script.

It writes the files that the build system cannot template:

  Telegram/SourceFiles/core/branding.h          the constants for the C++ side
  lib/xdg/polysrv.desktop                       the desktop entry
  lib/xdg/polysrv.service                       the D-Bus service
  lib/xdg/polysrv.metainfo.xml                  the AppStream metadata
  Telegram/Resources/winrc/Telegram.rc          the Windows resources
  Telegram/Resources/winrc/Updater.rc           the Windows updater resources
  Telegram/build/setup.iss                      the Inno Setup script
  Telegram/Resources/uwp/AppX/AppxManifest.xml  the AppX manifest

Everything else reads the values from Telegram/build/branding at configure
time, through Telegram/cmake/branding.cmake, or from the generated header.

Usage:
    python3 apply_branding.py            apply the branding
    python3 apply_branding.py --check    report the differences, change nothing
"""

import os
import re
import sys

ScriptDirectory = os.path.dirname(os.path.abspath(__file__))
RepositoryRoot = os.path.normpath(os.path.join(ScriptDirectory, '..', '..'))

BrandingFile = os.path.join(ScriptDirectory, 'branding')


def ReadBranding():
    values = {}
    with open(BrandingFile, encoding='utf-8') as file:
        for line in file:
            line = line.strip()
            if not line or line.startswith('#'):
                continue
            match = re.match(r'^([A-Za-z0-9_]+)[ \t]+(.+)$', line)
            if not match:
                raise ValueError('Cannot parse branding line: ' + line)
            values[match.group(1)] = match.group(2)
    return values


def ReadFile(path):
    with open(path, encoding='utf-8', newline='') as file:
        return file.read()


def WriteFile(path, content):
    os.makedirs(os.path.dirname(path), exist_ok=True)
    with open(path, 'w', encoding='utf-8', newline='') as file:
        file.write(content)


def ReplaceValue(content, pattern, value, path, label, changes, newPattern=None):
    """Replaces the value of a pattern, reporting whether it moved.

    The value is always the second capture group of the pattern. When the
    pattern no longer matches but newPattern does, the file already holds the
    wanted value and nothing is done.
    """
    match = re.search(pattern, content)
    if not match:
        if newPattern and re.search(newPattern, content):
            return content
        raise ValueError('Cannot find ' + label + ' in ' + path)
    old = match.group(2)
    if old == value:
        return content
    content = content[:match.start(2)] + value + content[match.end(2):]
    changes.append((path, label, old, value))
    return content


def EnsureDefine(content, name, value, path, changes):
    """Adds a define when the file does not have it yet."""
    pattern = r'(#define\s+' + re.escape(name) + r'\s+")([^"]*)(")'
    if re.search(pattern, content):
        return ReplaceValue(content, pattern, value, path, name, changes)
    ending = '\r\n' if '\r\n' in content else '\n'
    line = '#define %s "%s"%s' % (name, value, ending)
    anchor = re.search(r'(#define\s+MyAppId\s+"[^"]*"\r?\n)', content)
    if not anchor:
        raise ValueError('Cannot find the define anchor in ' + path)
    content = content[:anchor.end(1)] + line + content[anchor.end(1):]
    changes.append((path, name, '-', value))
    return content


def ReplaceDefine(content, name, value, path, changes):
    pattern = r'(#define\s+' + re.escape(name) + r'\s+")([^"]*)(")'
    return ReplaceValue(content, pattern, value, path, name, changes)


def WriteGenerated(path, content, changes):
    """Writes a file the script owns entirely."""
    label = os.path.relpath(path, RepositoryRoot).replace('\\', '/')
    old = ReadFile(path) if os.path.exists(path) else None
    if old == content:
        return
    WriteFile(path, content)
    changes.append((label, 'generated', '-', 'updated'))


def GenerateHeader(values):
    lines = [
        '/*',
        'This file is part of PolySRV Desktop,',
        'a Telegram Desktop fork for the PolySRV multi-server client.',
        '',
        'For license and copyright information please follow this link:',
        'https://github.com/telegramdesktop/tdesktop/blob/master/LEGAL',
        '*/',
        '#pragma once',
        '',
        '#include "base/const_string.h"',
        '',
        'namespace Branding {',
        '',
        '// The name of the product. The full name is used where the application',
        '// is named in prose, the short name where little space is available.',
        'constexpr auto AppName = "%s"_cs;' % values['AppName'],
        'constexpr auto ShortAppName = "%s"_cs;' % values['ShortAppName'],
        '',
        '// The Qt application name and the company the product is published as.',
        'constexpr auto ApplicationName = "%s"_cs;' % values['ApplicationName'],
        'constexpr auto CompanyName = "%s"_cs;' % values['CompanyName'],
        '',
        '// The identity of the installed application: the desktop entry, the',
        '// D-Bus service and the AppStream metadata all use it.',
        'constexpr auto BundleIdentifier = "%s"_cs;' % values['BundleIdentifier'],
        'constexpr auto MacOSBundleIdentifier = "%s"_cs;'
        % values['MacOSBundleIdentifier'],
        '',
        '// The executable, and the macOS bundle built around it.',
        'constexpr auto OutputName = "%s"_cs;' % values['OutputName'],
        '',
        '// The custom URL scheme the application answers to.',
        'constexpr auto UrlScheme = "%s"_cs;' % values['UrlScheme'],
        '',
        '// Where the project lives.',
        'constexpr auto Homepage = "%s"_cs;' % values['Homepage'],
        '',
        '} // namespace Branding',
        '',
    ]
    return '\n'.join(lines)


def GenerateDesktopEntry(values):
    return """[Desktop Entry]
Name={AppName}
Comment=New era of messaging
TryExec={OutputName}
Exec={OutputName} -- %U
Icon={BundleIdentifier}
Terminal=false
StartupWMClass={ApplicationName}
Type=Application
Categories=Chat;Network;InstantMessaging;Qt;
MimeType=x-scheme-handler/tg;x-scheme-handler/tonsite;x-scheme-handler/ton;x-scheme-handler/{UrlScheme};
Keywords=tg;chat;im;messaging;messenger;sms;tdesktop;polysrv;
Actions=quit;
DBusActivatable=true
SingleMainWindow=true
X-GNOME-UsesNotifications=true
X-GNOME-SingleWindow=true

[Desktop Action quit]
Exec={OutputName} -quit
Name=Quit {ShortAppName}
Icon=application-exit
""".format(**values)


def GenerateService(values):
    return """[D-BUS Service]
Name={BundleIdentifier}
Exec=@CMAKE_INSTALL_FULL_BINDIR@/{OutputName}
""".format(**values)


def GenerateAppStream(values):
    return """<?xml version="1.0" encoding="utf-8"?>
<component type="desktop">
    <id>{BundleIdentifier}</id>
    <metadata_license>CC0-1.0</metadata_license>
    <project_license>GPL-3.0</project_license>
    <name>{ShortAppName}</name>
    <summary>New era of messaging</summary>
    <description>
        <p>PolySRV is a Telegram Desktop fork that lets you sign in to more than one server from a single application.</p>
        <p>Choose a server when you add an account: the official Telegram network, or any self-hosted MTProto backend you point the client at. Each account keeps its own server binding, its own session identity and its own local state.</p>
        <p>PolySRV is a fork of Telegram Desktop and is not affiliated with Telegram FZ-LLC. Telegram itself remains a separate project with its own clients and its own releases.</p>
    </description>
    <categories>
        <category>Network</category>
        <category>InstantMessaging</category>
    </categories>
    <developer id="org.polysrv"><name>{CompanyName}</name></developer>
    <url type="homepage">{Homepage}</url>
    <url type="bugtracker">{Homepage}/issues</url>
    <url type="vcs-browser">{Homepage}</url>
    <url type="contribute">{Homepage}/blob/dev/.github/CONTRIBUTING.md</url>
    <branding>
        <color type="primary" scheme_preference="light">#{BrandColor}</color>
        <color type="primary" scheme_preference="dark">#{BrandColor}</color>
    </branding>
    <keywords>
        <keyword>telegram</keyword>
        <keyword>polysrv</keyword>
        <keyword>tdesktop</keyword>
        <keyword>messaging</keyword>
        <keyword>messenger</keyword>
        <keyword>chat</keyword>
        <keyword>im</keyword>
    </keywords>
    <requires>
        <!-- windowMinHeight from Telegram/SourceFiles/window/window.style -->
        <display_length compare="ge">480</display_length>
        <internet>always</internet>
        <!-- on-screen keyboards are too big for current minimal display height -->
        <control>keyboard</control>
    </requires>
    <supports>
        <control>pointing</control>
        <control>touch</control>
    </supports>
    <content_rating type="oars-1.1">
        <content_attribute id="violence-cartoon">none</content_attribute>
        <content_attribute id="violence-fantasy">none</content_attribute>
        <content_attribute id="violence-realistic">none</content_attribute>
        <content_attribute id="violence-bloodshed">none</content_attribute>
        <content_attribute id="violence-sexual">none</content_attribute>
        <content_attribute id="violence-desecration">none</content_attribute>
        <content_attribute id="violence-slavery">none</content_attribute>
        <content_attribute id="violence-worship">none</content_attribute>
        <content_attribute id="drugs-alcohol">none</content_attribute>
        <content_attribute id="drugs-narcotics">none</content_attribute>
        <content_attribute id="drugs-tobacco">none</content_attribute>
        <content_attribute id="sex-nudity">none</content_attribute>
        <content_attribute id="sex-themes">none</content_attribute>
        <content_attribute id="sex-homosexuality">none</content_attribute>
        <content_attribute id="sex-prostitution">none</content_attribute>
        <content_attribute id="sex-adultery">none</content_attribute>
        <content_attribute id="sex-appearance">none</content_attribute>
        <content_attribute id="language-profanity">none</content_attribute>
        <content_attribute id="language-humor">none</content_attribute>
        <content_attribute id="language-discrimination">none</content_attribute>
        <content_attribute id="social-chat">intense</content_attribute>
        <content_attribute id="social-info">none</content_attribute>
        <content_attribute id="social-audio">intense</content_attribute>
        <content_attribute id="social-location">none</content_attribute>
        <content_attribute id="social-contacts">intense</content_attribute>
        <content_attribute id="money-purchasing">intense</content_attribute>
        <content_attribute id="money-gambling">none</content_attribute>
        <content_attribute id="money-advertising">moderate</content_attribute>
    </content_rating>
    <launchable type="desktop-id">{BundleIdentifier}.desktop</launchable>
    <provides>
        <binary>{OutputName}</binary>
        <dbus type="session">{BundleIdentifier}</dbus>
        <mediatype>x-scheme-handler/tg</mediatype>
        <mediatype>x-scheme-handler/tonsite</mediatype>
        <mediatype>x-scheme-handler/ton</mediatype>
        <mediatype>x-scheme-handler/{UrlScheme}</mediatype>
    </provides>
</component>
""".format(**values)


def ApplyResourceScript(path, values, changes, updater):
    content = ReadFile(path)
    description = values['AppName'] + (' Updater' if updater else '')
    content = ReplaceValue(
        content, r'(VALUE\s+"CompanyName",\s*")([^"]*)(")',
        values['CompanyName'], path, 'CompanyName', changes)
    content = ReplaceValue(
        content, r'(VALUE\s+"FileDescription",\s*")([^"]*)(")',
        description, path, 'FileDescription', changes)
    content = ReplaceValue(
        content, r'(VALUE\s+"ProductName",\s*")([^"]*)(")',
        values['AppName'], path, 'ProductName', changes)
    WriteFile(path, content)


def ApplySetupScript(path, values, changes):
    content = ReadFile(path)
    for name, key in (
        ('MyAppShortName', 'ShortAppName'),
        ('MyAppName', 'AppName'),
        ('MyAppPublisher', 'CompanyName'),
        ('MyAppURL', 'Homepage'),
        ('MyAppId', 'AppId'),
    ):
        content = ReplaceDefine(content, name, values[key], path, changes)
    content = ReplaceDefine(
        content, 'MyAppExeName', values['OutputName'] + '.exe', path, changes)
    content = EnsureDefine(
        content, 'MyOutputBasePrefix', values['OutputBasePrefix'], path, changes)
    # The installer refers to its own defines, so that renaming the product
    # only ever changes the values above.
    content = ReplaceValue(
        content,
        r'(Source:\s+"\{#ReleasePath\}\\)([^"\r\n]*)(";\s+DestDir:\s+"\{app\}")',
        '{#MyAppExeName}', path, 'installed executable', changes,
        r'Source:\s+"\{#ReleasePath\}\\{#MyAppExeName}"')
    content = ReplaceValue(
        content, r'(UninstallDisplayIcon=\{app\}\\)([^"\r\n]*)',
        '{#MyAppExeName}', path, 'uninstall icon', changes,
        r'UninstallDisplayIcon=\{app\}\\{#MyAppExeName\}')
    for suffix in ('arm64', 'x64', ''):
        tail = ('-' + suffix if suffix else '') + '.'
        content = ReplaceValue(
            content,
            r'(#define\s+MyOutputBaseFilename\s+")(tsetup-?' + suffix + r'\.)',
            '{#MyOutputBasePrefix}' + tail,
            path, 'installer name ' + (suffix or 'default'), changes,
            r'#define\s+MyOutputBaseFilename\s+"\{#MyOutputBasePrefix\}'
            + re.escape(tail))
    WriteFile(path, content)


def EnsureProtocol(content, scheme, path, changes):
    """Adds the custom URL scheme to the AppX manifest when it is missing."""
    if re.search(r'<uap3:Protocol\s+Name="' + re.escape(scheme) + r'"', content):
        return content
    ending = '\r\n' if '\r\n' in content else '\n'
    block = (
        '        <uap3:Extension Category="windows.protocol">%s'
        '          <uap3:Protocol Name="%s" Parameters="-- &quot;%%1&quot;" />%s'
        '        </uap3:Extension>%s' % (ending, scheme, ending, ending)
    )
    anchor = re.search(r'( *</Extensions>)', content)
    if not anchor:
        raise ValueError('Cannot find the extensions anchor in ' + path)
    content = content[:anchor.start(1)] + block + content[anchor.start(1):]
    changes.append((path, 'Protocol ' + scheme, '-', 'added'))
    return content


def ApplyAppxManifest(path, values, changes):
    content = ReadFile(path)
    content = ReplaceValue(
        content, r'(<Identity\s+Name=")([^"]*)(")',
        values['AppXIdentityName'], path, 'Identity Name', changes)
    content = ReplaceValue(
        content, r'(<DisplayName>)([^<]*)(</DisplayName>)',
        values['AppName'], path, 'DisplayName', changes)
    content = ReplaceValue(
        content, r'(<PublisherDisplayName>)([^<]*)(</PublisherDisplayName>)',
        values['CompanyName'], path, 'PublisherDisplayName', changes)
    content = ReplaceValue(
        content, r'(<Description>)([^<]*)(</Description>)',
        values['Description'], path, 'Description', changes)
    content = ReplaceValue(
        content, r'(Executable=")([^"]*)(")',
        values['OutputName'] + '.exe', path, 'Executable', changes)
    content = ReplaceValue(
        content, r'(DisplayName=")([^"]*)(")',
        values['AppName'], path, 'VisualElements DisplayName', changes)
    content = ReplaceValue(
        content, r'(BackgroundColor=")([^"]*)(")',
        '#' + values['BrandColor'], path, 'BackgroundColor', changes)
    content = ReplaceValue(
        content, r'(TaskId=")([^"]*)(")',
        values['AppXStartupTaskId'], path, 'TaskId', changes)
    content = EnsureProtocol(content, values['UrlScheme'], path, changes)
    WriteFile(path, content)


def ApplyLiterals(values, changes):
    """Renames the binary in the sources that cannot use the C++ constants.

    The Updater is built without Qt, so it cannot include the generated
    header, and the names it works with are plain string literals there.
    """
    output = values['OutputName']
    files = [
        ('Telegram/SourceFiles/_other/updater_win.cpp', [
            ('L"Telegram.exe"', 'L"%s.exe"' % output),
            ('L"Telegram Desktop"', 'L"%s"' % values['AppName']),
            ('L"Telegram FZ-LLC"', 'L"%s"' % values['CompanyName']),
            ('L"https://desktop.telegram.org"',
             'L"%s"' % values['Homepage']),
        ]),
        ('Telegram/SourceFiles/_other/updater_linux.cpp', [
            ('"Telegram"', '"%s"' % output),
        ]),
        ('Telegram/SourceFiles/_other/updater_osx.m', [
            ('@"Telegram.app"', '@"%s.app"' % output),
            ('@"/Contents/MacOS/Telegram"',
             '@"/Contents/MacOS/%s"' % output),
        ]),
        ('Telegram/SourceFiles/platform/linux/update_install_linux.cpp', [
            ('"Telegram"', '"%s"' % output),
        ]),
        ('Telegram/SourceFiles/_other/startup_task_win.cpp', [
            ('L"\\\\Telegram.exe"', 'L"\\\\%s.exe"' % output),
        ]),
        ('Telegram/SourceFiles/platform/win/windows_app_user_model_id.cpp', [
            ('L"Telegram.TelegramDesktop.Store"',
             'L"%s.Store"' % values['AppXIdentityName']),
            ('L"Telegram.TelegramDesktop"',
             'L"%s"' % values['AppXIdentityName']),
            ('u"Telegram Desktop/Telegram.lnk"',
             'u"%s/%s.lnk"' % (values['AppName'], output)),
            ('u"Telegram.lnk"', 'u"%s.lnk"' % output),
            ('u"TelegramAlpha.lnk"', 'u"%sAlpha.lnk"' % output),
        ]),
        ('Telegram/SourceFiles/platform/win/windows_autostart_task.cpp', [
            ('L"TelegramStartupTask"', 'L"%s"' % values['AppXStartupTaskId']),
        ]),
        ('Telegram/SourceFiles/tests/test_update_unpack.cpp', [
            ('"Telegram"', '"%s"' % output),
        ]),
        ('Telegram/SourceFiles/boxes/about_box.cpp', [
            ('"https://github.com/telegramdesktop/tdesktop"',
             '"%s"' % values['Homepage']),
        ]),
    ]
    for relative, replacements in files:
        path = os.path.join(RepositoryRoot, *relative.split('/'))
        content = ReadFile(path)
        for old, new in replacements:
            if old not in content:
                continue
            content = content.replace(old, new)
            changes.append((relative, old, old, new))
        WriteFile(path, content)


def main(argv):
    check = '--check' in argv
    values = ReadBranding()
    changes = []

    # The files the script owns.
    WriteGenerated(
        os.path.join(RepositoryRoot, 'Telegram', 'SourceFiles', 'core',
                     'branding.h'),
        GenerateHeader(values),
        changes)
    xdg = os.path.join(RepositoryRoot, 'lib', 'xdg')
    WriteGenerated(
        os.path.join(xdg, 'polysrv.desktop'),
        GenerateDesktopEntry(values),
        changes)
    WriteGenerated(
        os.path.join(xdg, 'polysrv.service'),
        GenerateService(values),
        changes)
    WriteGenerated(
        os.path.join(xdg, 'polysrv.metainfo.xml'),
        GenerateAppStream(values),
        changes)

    # The files the script fills the values into.
    resources = os.path.join(RepositoryRoot, 'Telegram', 'Resources', 'winrc')
    ApplyResourceScript(
        os.path.join(resources, 'Telegram.rc'), values, changes, False)
    ApplyResourceScript(
        os.path.join(resources, 'Updater.rc'), values, changes, True)
    ApplySetupScript(
        os.path.join(ScriptDirectory, 'setup.iss'), values, changes)
    ApplyAppxManifest(
        os.path.join(RepositoryRoot, 'Telegram', 'Resources', 'uwp', 'AppX',
                     'AppxManifest.xml'),
        values,
        changes)
    ApplyLiterals(values, changes)

    for path, label, old, new in changes:
        print('%-52s %-24s %s -> %s' % (path, label, old, new))
    if not changes:
        print('The branding is already applied.')
    elif check:
        print('%d change(s) pending.' % len(changes))
        return 1
    return 0


if __name__ == '__main__':
    sys.exit(main(sys.argv[1:]))
