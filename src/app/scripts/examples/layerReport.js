// Walk all layers of the active scene and print a report to the
// script console. Shows layer()/layers(), name/visible/selected and
// reading animated values with valueAtTime().

registerCommand("Layer Report", function() {
    var scene = app.activeScene;
    if (!scene) {
        alert("Please open or create a scene first.");
        return;
    }

    var layers = scene.layers;
    $.writeln("=== Scene \"" + scene.name + "\" ===");
    $.writeln("Size: " + scene.width + "x" + scene.height +
              " @ " + scene.fps + " fps, duration " +
              scene.duration + "s");
    $.writeln("Layers: " + layers.length);
    $.writeln("");

    for (var i = 0; i < layers.length; i++) {
        var layer = layers[i];
        var pos = layer.property("position");
        var p = pos.value;
        var line = (i + 1) + ". " + layer.name +
                   "  [pos " + p[0] + ", " + p[1] + "]";
        if (pos.numKeys > 0) {
            line += "  (animated, " + pos.numKeys + " keys)";
        }
        if (layer.selected) { line += "  *selected*"; }
        if (!layer.visible) { line += "  (hidden)"; }
        $.writeln(line);
    }
});
