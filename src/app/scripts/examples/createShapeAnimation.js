// Create a rectangle that slides across the scene and fades out,
// using AE-style keyframe APIs (seconds-based, like setValueAtTime).

registerCommand("Create Sliding Box", function() {
    var scene = app.activeScene;
    if (!scene) {
        alert("Please open or create a scene first.");
        return;
    }

    var w = 200;
    var h = 120;
    var y = scene.height / 2 - h / 2;

    var box = scene.addRect("Sliding Box", 0, y, w, h);
    if (!box) {
        alert("Failed to create the rectangle.");
        return;
    }

    // keyframes on position (seconds, AE convention)
    var pos = box.property("position");
    pos.setValueAtTime(0, [-w / 2, y + h / 2]);
    pos.setValueAtTime(2, [scene.width / 2, y + h / 2]);
    pos.setValueAtTime(4, [scene.width + w / 2, y + h / 2]);

    // fade out via layer opacity (0-100)
    box.opacity = 100;

    $.writeln("Created \"" + box.name + "\" with " +
              pos.numKeys + " position keys.");
    $.writeln("Key times: " + pos.keyTime(1) + "s, " +
              pos.keyTime(2) + "s, " + pos.keyTime(3) + "s");
});

registerCommand("Create Text Layer", function() {
    var scene = app.activeScene;
    if (!scene) {
        alert("Please open or create a scene first.");
        return;
    }
    var text = scene.addText("Title", "Made with Friction JS");
    if (text) {
        text.property("position").setValue(
            [scene.width / 2, scene.height / 2]);
        $.writeln("Created text layer \"" + text.name + "\".");
    }
});
