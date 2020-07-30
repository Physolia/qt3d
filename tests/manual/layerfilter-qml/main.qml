/****************************************************************************
**
** Copyright (C) 2017 Klaralvdalens Datakonsult AB (KDAB).
** Contact: https://www.qt.io/licensing/
**
** This file is part of the Qt3D module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:BSD$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** BSD License Usage
** Alternatively, you may use this file under the terms of the BSD license
** as follows:
**
** "Redistribution and use in source and binary forms, with or without
** modification, are permitted provided that the following conditions are
** met:
**   * Redistributions of source code must retain the above copyright
**     notice, this list of conditions and the following disclaimer.
**   * Redistributions in binary form must reproduce the above copyright
**     notice, this list of conditions and the following disclaimer in
**     the documentation and/or other materials provided with the
**     distribution.
**   * Neither the name of The Qt Company Ltd nor the names of its
**     contributors may be used to endorse or promote products derived
**     from this software without specific prior written permission.
**
**
** THIS SOFTWARE IS PROVIDED BY THE COPYRIGHT HOLDERS AND CONTRIBUTORS
** "AS IS" AND ANY EXPRESS OR IMPLIED WARRANTIES, INCLUDING, BUT NOT
** LIMITED TO, THE IMPLIED WARRANTIES OF MERCHANTABILITY AND FITNESS FOR
** A PARTICULAR PURPOSE ARE DISCLAIMED. IN NO EVENT SHALL THE COPYRIGHT
** OWNER OR CONTRIBUTORS BE LIABLE FOR ANY DIRECT, INDIRECT, INCIDENTAL,
** SPECIAL, EXEMPLARY, OR CONSEQUENTIAL DAMAGES (INCLUDING, BUT NOT
** LIMITED TO, PROCUREMENT OF SUBSTITUTE GOODS OR SERVICES; LOSS OF USE,
** DATA, OR PROFITS; OR BUSINESS INTERRUPTION) HOWEVER CAUSED AND ON ANY
** THEORY OF LIABILITY, WHETHER IN CONTRACT, STRICT LIABILITY, OR TORT
** (INCLUDING NEGLIGENCE OR OTHERWISE) ARISING IN ANY WAY OUT OF THE USE
** OF THIS SOFTWARE, EVEN IF ADVISED OF THE POSSIBILITY OF SUCH DAMAGE."
**
** $QT_END_LICENSE$
**
****************************************************************************/

import Qt3D.Core 2.0
import Qt3D.Render 2.10
import Qt3D.Input 2.0
import Qt3D.Extras 2.0

Entity {
    id: sceneRoot

    Camera {
        id: camera
        projectionType: CameraLens.PerspectiveProjection
        fieldOfView: 45
        aspectRatio: 16/9
        nearPlane : 0.1
        farPlane : 1000.0
        position: Qt.vector3d( 0.0, 0.0, 3.0 )
        upVector: Qt.vector3d( 0.0, 1.0, 0.0 )
        viewCenter: Qt.vector3d( 0.0, 0.0, 0.0 )
    }

    Camera {
        id: backgroundCamera
        projectionType: CameraLens.OrthographicProjection
        left: -0.25
        bottom: -0.25
        top: 0.25
        right: 0.25
        nearPlane: 0.1
        farPlane: 1.5
        position: Qt.vector3d( 0.0, 0.0, 1.0 )
        upVector: Qt.vector3d( 0.0, 1.0, 0.0 )
        viewCenter: Qt.vector3d( 0.0, 0.0, 0.0 )
    }

    Layer {
        id: backgroundLayer
        recursive: true
    }

    Layer {
        id: sceneLayer
        recursive: true
    }

    Layer {
        id: secondBackgroundLayer
    }

    RenderSettings {
        id : external_forward_renderer
        activeFrameGraph :

            RenderSurfaceSelector {
            Viewport {
                normalizedRect: Qt.rect(0,0,1,1)

                ClearBuffers {
                    buffers: ClearBuffers.ColorDepthStencilBuffer
                    clearColor: "white"

                    LayerFilter {
                        layers: [
                            backgroundLayer,
                            secondBackgroundLayer
                        ]

                        CameraSelector {
                            camera: backgroundCamera
                        }
                    }
                }

                ClearBuffers {
                    buffers: ClearBuffers.DepthStencilBuffer

                    LayerFilter {
                        layers: [
                            backgroundLayer,
                        ]
                        filterMode: LayerFilter.DiscardAnyMatchingLayers

                        CameraSelector {
                            camera: camera
                        }
                    }
                }
            }
        }
    }

    OrbitCameraController {
        camera: camera
    }

    InputSettings {
        id: inputSettings
    }

    components: [external_forward_renderer, inputSettings]

    PlaneMesh {
        id: backgroundMesh
        width: 0.5
        height: 0.5
    }

    Transform {
        id: backgroundTransform
        rotation: fromAxisAndAngle(Qt.vector3d(1,0,0), 90)
    }

    Transform {
        id: smallBackgroundTransform
        translation: Qt.vector3d(0,0.5,0)
        scale: 0.5
    }

    PhongMaterial {
        id: backgroundMaterial
        ambient: "black"
    }

    PhongMaterial {
        id: smallBackgroundMaterial
        ambient: "red"
    }

    GeometryRenderer {
        id: mesh
        view: PlaneMesh {
            width: 1
            height: 1
        }
    }

    Transform {
        id: transform
        translation: Qt.vector3d(0,0,-1)
        rotation: fromAxisAndAngle(Qt.vector3d(1,0,0), 90)
    }

    PhongMaterial {
        id: material
        ambient: "blue"
    }

    Entity {
        id: backgroundFilterEntity
        components: [backgroundLayer]

        Entity {
            id: backgroundEntity
            objectName: "backgroundEntity"
            components: [backgroundMesh, backgroundMaterial, backgroundTransform]

            Entity {
                id: smallBackground
                objectName: "smallBackground"
                components: [backgroundMesh, smallBackgroundMaterial, smallBackgroundTransform]
            }
        }
    }

    Entity {
        components: [sceneLayer]

        Entity {
            id: mainEntity
            objectName: "mainEntity"
            components: [ mesh, material, transform ]
        }
    }
}
