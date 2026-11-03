# Screen reader access

LYNX SHARE works with VoiceOver (macOS), NVDA, JAWS and Narrator (Windows). The design follows OSARA for REAPER: a keyboard cursor in each panel, short spoken reports, the menus of the current item, and a parameter dialog for every value. Nothing on screen changes; the keys below act on the panels' own selection, so every existing command and shortcut works on what you reached.

The layer turns on by itself while a screen reader runs. **Cmd+Option+Shift+A** (Windows: Ctrl+Alt+Shift+A) turns it on or off by hand. Every key can be changed in Options › Manage shortcuts.

On macOS, Ctrl below means Cmd and Alt means Option. The menus are in the macOS menu bar (VO+M).

## Everywhere

| Key | Action |
| --- | --- |
| Alt+F10, Alt+Shift+F | Open the menu bar (Windows) |
| Shift+F10, Shift+Enter | Menu of the current item (the right-click menu) |
| Alt+Shift+V | Parameters of the current item |
| Alt+Shift+G | Project parameters (tempo, beats per bar, swing, master pitch) |
| Alt+Shift+P | Report the position (the playhead while playing, else the cursor) |
| Alt+Shift+B | Report the tempo and time signature |
| Alt+Shift+I | Report the transport, mode and pattern |
| Alt+Shift+C | Report the current item |
| Alt+Shift+M | Mute or unmute the current item |
| Alt+Shift+S | Solo the current item |
| Alt+Shift+Left / Right | Previous / next marker |
| Alt+Shift+O | Accessibility settings |
| Alt+Shift+N | Go to the pattern selector; again, back to where you were |
| Alt+Shift+K | Metronome on / off (also Ctrl+M) |
| Alt+Shift+U | Count-in on / off (also Ctrl+P) |
| Alt+Shift+W | Recording waits for the first note on / off (also Ctrl+I) |
| Alt+Shift+T | Typing keyboard as a piano on / off (also Ctrl+T) |
| Alt+Shift+D | Split the current pattern by channel |
| Alt+Shift+L | Link the selected channels to an insert, picked from a list |
| Alt+Shift+1 / 2 / 4 / 8 | Fill each step / 2 / 4 / 8 steps of the selected channels |

The grid keys go through the fixed sizes, 1/6 step to Bar, and say the new one; they set the editor's own grid, or the toolbar's while the editor follows it. The "Line" grid follows the zoom, so the cursor's step changes with it until you pick a fixed size.

Panels are switched with their usual keys: F5 Playlist, F6 Channel Rack, F7 Piano Roll, F9 Mixer, Alt+F8 Browser. The same key on the focused panel hides it. The keyboard focus moves into the panel, and the panel and its current item are read. Moving the VoiceOver cursor into a panel makes it the focused one too.

## Window layout

The main window is a list of groups. With VoiceOver, VO+Shift+Down enters a group and VO+Shift+Up leaves it.

- The toolbar groups: Transport, Master volume and pitch, Recording options, Snap and pattern, Window shortcuts, File and undo, CPU, memory and polyphony, and Output monitor. Tempo, master volume, master pitch and the song position are sliders.
- One group per open panel, named after it: the panel's keyboard target (its current item), its tools and its title bar buttons.

The keyboard follows the VoiceOver cursor onto the toolbar controls: there the arrows change the slider or combo box (Page Up / Down ten steps, Home / End the ends) and Enter presses a button, while the global keys (Space, the F-keys, Ctrl+S...) still work. The focused panel's key (F5, F9...) brings the keyboard back to it.

## Playlist

| Key | Action |
| --- | --- |
| Up / Down | Previous / next track |
| Left / Right | Move the cursor one grid step |
| Alt+Left / Right | Move the cursor one bar |
| Home / End | Start / end of the song |
| Tab / Shift+Tab | Next / previous clip on the track (selects it) |
| Enter | Open the current clip (pattern in the Piano Roll, audio in its editor, automation) |
| Alt+Enter | Place the current pattern or brush at the cursor |
| Ctrl+E | Split the clip at the cursor (the second part becomes the current clip) |
| Ctrl+V | Paste at the cursor; the top clip lands on the current track |
| [ / ] | Start / end the time selection at the cursor |
| Alt+Shift+Up / Down | Coarser / finer grid |

While stopped in song mode the playhead follows the cursor, so Space plays from there. When playback stops or pauses, the cursor moves to where it stopped (in pattern mode, the Piano Roll's cursor). The usual Delete, Ctrl+C, Ctrl+X, Ctrl+B and Ctrl+A act on the selected clips and are reported.

## Piano Roll

| Key | Action |
| --- | --- |
| Up / Down | Move the cursor one semitone (plays the note) |
| Left / Right | Move the cursor one grid step |
| Alt+Left / Right | Move the cursor one bar |
| Home / End | Start / end of the pattern |
| Tab / Shift+Tab | Next / previous chord (selects and plays it) |
| Alt+Up / Down | Next higher / lower note in the chord |
| Enter | Add a note at the cursor, or delete the note that starts there (inside a longer note, the new note follows it and the longer note ends there) |
| Alt+Shift+Up / Down | Coarser / finer grid |
| Ctrl+Enter | Select or deselect the note at the cursor |

The Piano Roll edits the selected channel. Shift+arrows move the selected notes a semitone or a grid step, Ctrl+Up / Down an octave. The menu of the current item adds Note properties, Parameters, and the Select, Tools and MIDI menus.

## Channel Rack

The usual Up / Down select channels.

Patterns › Split by channel makes one pattern per channel of the current pattern. In the Playlist each clip of the pattern becomes one clip per channel, on its track and the ones below; a pattern not yet in the Playlist gets them at bar 1, below the last used track.

| Key | Action |
| --- | --- |
| Left / Right | Previous / next step |
| Home / End | First / last step |
| Enter | Turn the step on or off |

The parameters of a channel list its volume, pan, mute, mixer insert, and every parameter of its instrument (plugins and the Sampler).

## Mixer

| Key | Action |
| --- | --- |
| Left / Right | Previous / next mixer track |
| Home / End | Master / last mixer track in use |
| Up / Down | Volume up / down 1 dB |
| Shift+Left / Right | Pan left / right 5% |
| Tab / Shift+Tab | Next / previous effect slot |
| Enter | Open the effect slot (an empty slot opens the list of effects) |
| Ctrl+L | Link the selected channels to this insert |

To route a channel to an insert of your choice: select it in the Channel Rack and press Alt+Shift+L to pick the insert from a list, or press F9, go to the insert with Left / Right and press Ctrl+L. (Ctrl+L in the Channel Rack sends the selected channels to free inserts instead.) The channel's parameters (Alt+Shift+V) also have its mixer insert.

The parameters of a mixer track list volume, pan, stereo separation, mute, solo, phase, channel swap and its sends. The parameters of an effect slot list on/off, mix and every parameter of the effect.

## Browser

| Key | Action |
| --- | --- |
| Up / Down | Previous / next file or folder (a sample plays as a preview) |
| Left / Right | Close / open the folder |
| Space | Play or stop the preview |
| Enter | Load the file into the selected channel |
| Shift+Up / Down | Load the previous / next file into the selected channel |
| Ctrl+Enter | Open the file in a new channel (a sample: a new Sampler) |
| Alt+Enter | Add the sample to the Playlist: at the Playlist cursor, on its current track |
| Ctrl+F | Search |

The menu of the current item (Shift+F10) has the file's other actions. With nothing selected it opens the Browser's options, which have Add folder... and Show folders... (the folders the Browser lists).

Ctrl+Space plays or pauses the project while the Browser has the focus.

## Parameter dialog

A filter field, the parameter list, a value slider and a field to type a value; Tab moves between them. On the slider, arrows step the value, Page Up / Down move ten steps, Home / End go to the ends. Typed values use the same units as the reading (for example `-6 dB`, `C5`, `40% left`).

## Settings

Options › Accessibility settings (Alt+Shift+O):

- **Accessibility mode**: Automatic (with a screen reader), On, Off.
- **Positions in minutes and seconds** instead of bars and beats.
- **Report the position when the cursor moves**.
- **Report play, stop, record and modes**.
- **Report undo and redo**.
- **Play notes while moving in the Piano Roll**.
- **Report track, channel and insert numbers**.

Settings are saved in `accessibility.json` in the LYNX SHARE settings folder.

## Not covered yet

- The built-in instruments' and effects' own editor windows are drawn by LYNX SHARE and not readable; their values are all in the parameter dialog.
- Third-party plugin windows are as accessible as the plugin makes them; their parameters are in the parameter dialog.
- Level meters are not reported.
