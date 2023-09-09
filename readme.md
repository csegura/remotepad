# RemotePad

Crazy idea to draw over screen using any tablet or phone :-) of course on linux

WORK IN PROGRESS

## Installation

Install on your toolbox

```
npm install remotepad -g
```

## Use

```
./remotepad.sh
```

### parameters

- none - select the window to use
- current - use the current window
- <id> - use the window with id number

Application runs a server (localhost:3000) ....

- Select target window
- Server remains in background waitting for activation

  - You can access to the client from your tablet using <your-ip>:3000

- On local

  - CTRL+SHIFT+d - begins drawing mode
    - You can draw using your mouse + left button
    - mouse right button clear screen and terminate drawing mode
  - CTRL+SHIFT+e - terminate drawing mode

  - In drawing mode
    - CTRL+SHIFT+r change foreground to red
    - CTRL+SHIFT+g change foreground to green
    - CTRL+SHIFT+b change foreground to blue
    - CTRL+SHIFT+s take an screenshot of current screen
    - CTRL+SHIFT+c clear screen
    - CTRL+SHIFT+u undo

....

### Use in Visual Studio Code

Add a keyboard shortcut I am using CTRL+Shift+9

![](samples/vs_command.png)

```
// Place your key bindings in this file to override the defaults
[{
    "key": "ctrl+shift+9",
    "command": "workbench.action.terminal.sendSequence",
    "args": { "text": "~/dev_local/remotepad/remotepad.sh current\u000D" }
}
]
```

#### Some usesful docs

- (https://w3c.github.io/pointerevents)
- node_modules/x11/lib/keysyms.js - see examples in x11 source code

#### x11 tools used

- xev - get keycodes and mouse events
- xwininfo - get info and windows id
- xprop - get window properties
