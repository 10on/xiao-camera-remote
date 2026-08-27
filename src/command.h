#pragma once

// Abstract, protocol-independent commands. Button input is translated to
// these before reaching any device driver (see Menu::handleButton); a
// driver silently ignores commands it doesn't understand. One command can
// fan out to every active device at once (e.g. MoveForward could start
// the slider while also starting a phone recording, once a phone driver
// exists).
enum class Command {
	MoveForward,
	MoveBackward,
	StopMove,
	Home,
	SpeedUp,
	SpeedDown,
	Record,
	StopRecord,
};
