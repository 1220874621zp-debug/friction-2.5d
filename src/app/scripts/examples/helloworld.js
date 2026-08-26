// Minimal script example: shows print(), alert() and app access.
// Scripts live in the folder opened via menu "Scripts > Open Scripts
// Folder". Reload them any time with "Scripts > Reload Scripts".

registerCommand("Hello World", function() {
    var scene = app.activeScene;
    if (!scene) {
        alert("Please open or create a scene first.");
        return;
    }
    $.writeln("Active scene: " + scene.name);
    $.writeln("Size: " + scene.width + "x" + scene.height +
              " @ " + scene.fps + " fps");
    $.writeln("Layers: " + scene.numLayers);
    alert("Hello from Friction JS!\nScene \"" + scene.name +
          "\" has " + scene.numLayers + " layer(s).");
});
