import QtQuick
import Render 1.0

PrintWorkspacePage {
    workspaceName: "realistic"
    rendererRole: ThreadRendererQmlItem.Realistic
    // This is another projection of the normal application Document, not a
    // renderer demo. Scene initialization is guarded by the shared workspace
    // context and is therefore safe whichever workspace becomes ready first.
    initializeScene: true
}
