/****************************************************************************
**
** Copyright (C) 2014 Klaralvdalens Datakonsult AB (KDAB).
** Contact: https://www.qt.io/licensing/
**
** This file is part of the Qt3D module of the Qt Toolkit.
**
** $QT_BEGIN_LICENSE:LGPL$
** Commercial License Usage
** Licensees holding valid commercial Qt licenses may use this file in
** accordance with the commercial license agreement provided with the
** Software or, alternatively, in accordance with the terms contained in
** a written agreement between you and The Qt Company. For licensing terms
** and conditions see https://www.qt.io/terms-conditions. For further
** information use the contact form at https://www.qt.io/contact-us.
**
** GNU Lesser General Public License Usage
** Alternatively, this file may be used under the terms of the GNU Lesser
** General Public License version 3 as published by the Free Software
** Foundation and appearing in the file LICENSE.LGPL3 included in the
** packaging of this file. Please review the following information to
** ensure the GNU Lesser General Public License version 3 requirements
** will be met: https://www.gnu.org/licenses/lgpl-3.0.html.
**
** GNU General Public License Usage
** Alternatively, this file may be used under the terms of the GNU
** General Public License version 2.0 or (at your option) the GNU General
** Public license version 3 or any later version approved by the KDE Free
** Qt Foundation. The licenses are as published by the Free Software
** Foundation and appearing in the file LICENSE.GPL2 and LICENSE.GPL3
** included in the packaging of this file. Please review the following
** information to ensure the GNU General Public License requirements will
** be met: https://www.gnu.org/licenses/gpl-2.0.html and
** https://www.gnu.org/licenses/gpl-3.0.html.
**
** $QT_END_LICENSE$
**
****************************************************************************/

#include "shader_p.h"
#include "renderlogging_p.h"

#include <QFile>
#include <QOpenGLContext>
#include <QOpenGLShaderProgram>
#include <QMutexLocker>
#include <qshaderprogram.h>
#include <Qt3DRender/private/attachmentpack_p.h>
#include <Qt3DRender/private/graphicscontext_p.h>
#include <Qt3DRender/private/qshaderprogram_p.h>
#include <Qt3DRender/private/stringtoint_p.h>
#include <Qt3DCore/qpropertyupdatedchange.h>

QT_BEGIN_NAMESPACE

using namespace Qt3DCore;

namespace Qt3DRender {
namespace Render {

Shader::Shader()
    : BackendNode()
    , m_program(nullptr)
    , m_isLoaded(false)
    , m_dna(0)
    , m_graphicsContext(nullptr)
{
    m_shaderCode.resize(static_cast<int>(QShaderProgram::Compute) + 1);
}

Shader::~Shader()
{
    // TO DO: ShaderProgram is leaked as of now
    // Fix that taking care that they may be shared given a same dna
}

void Shader::cleanup()
{
    // Remove this shader from the hash in the graphics context so
    // nothing tries to use it after it has been recycled
    if (m_graphicsContext)
        m_graphicsContext->removeProgram(dna(), peerId());

    QBackendNode::setEnabled(false);
    m_isLoaded = false;
    m_dna = 0;
    // TO DO: ShaderProgram is leaked as of now
    // Fix that taking care that they may be shared given a same dna
    m_program = nullptr;
    m_uniformsNames.clear();
    m_attributesNames.clear();
    m_uniformBlockNames.clear();
    m_uniforms.clear();
    m_attributes.clear();
    m_uniformBlocks.clear();
}

void Shader::initializeFromPeer(const Qt3DCore::QNodeCreatedChangeBasePtr &change)
{
    const auto typedChange = qSharedPointerCast<Qt3DCore::QNodeCreatedChange<QShaderProgramData>>(change);
    const auto &data = typedChange->data;

    for (int i = QShaderProgram::Vertex; i <= QShaderProgram::Compute; ++i)
        m_shaderCode[i].clear();

    m_shaderCode[QShaderProgram::Vertex] = data.vertexShaderCode;
    m_shaderCode[QShaderProgram::TessellationControl] = data.tessellationControlShaderCode;
    m_shaderCode[QShaderProgram::TessellationEvaluation] = data.tessellationEvaluationShaderCode;
    m_shaderCode[QShaderProgram::Geometry] = data.geometryShaderCode;
    m_shaderCode[QShaderProgram::Fragment] = data.fragmentShaderCode;
    m_shaderCode[QShaderProgram::Compute] = data.computeShaderCode;
    m_isLoaded = false;
    updateDNA();
}

QVector<QString> Shader::uniformsNames() const
{
    return m_uniformsNames;
}

QVector<QString> Shader::attributesNames() const
{
    return m_attributesNames;
}

QVector<QString> Shader::uniformBlockNames() const
{
    return m_uniformBlockNames;
}

QVector<QString> Shader::storageBlockNames() const
{
    return m_shaderStorageBlockNames;
}

QVector<QByteArray> Shader::shaderCode() const
{
    return m_shaderCode;
}

void Shader::sceneChangeEvent(const Qt3DCore::QSceneChangePtr &e)
{
    if (e->type() == PropertyUpdated) {
        QPropertyUpdatedChangePtr propertyChange = e.staticCast<QPropertyUpdatedChange>();
        QVariant propertyValue = propertyChange->value();

        if (propertyChange->propertyName() == QByteArrayLiteral("vertexSourceCode")) {
            m_shaderCode[QShaderProgram::Vertex] = propertyValue.toByteArray();
            m_isLoaded = false;
        } else if (propertyChange->propertyName() == QByteArrayLiteral("fragmentSourceCode")) {
            m_shaderCode[QShaderProgram::Fragment] = propertyValue.toByteArray();
            m_isLoaded = false;
        } else if (propertyChange->propertyName() == QByteArrayLiteral("tessellationControlSourceCode")) {
            m_shaderCode[QShaderProgram::TessellationControl] = propertyValue.toByteArray();
            m_isLoaded = false;
        } else if (propertyChange->propertyName() == QByteArrayLiteral("tessellationEvaluationSourceCode")) {
            m_shaderCode[QShaderProgram::TessellationEvaluation] = propertyValue.toByteArray();
            m_isLoaded = false;
        } else if (propertyChange->propertyName() == QByteArrayLiteral("geometrySourceCode")) {
            m_shaderCode[QShaderProgram::Geometry] = propertyValue.toByteArray();
            m_isLoaded = false;
        } else if (propertyChange->propertyName() == QByteArrayLiteral("computeSourceCode")) {
            m_shaderCode[QShaderProgram::Compute] = propertyValue.toByteArray();
            m_isLoaded = false;
        }
        if (!m_isLoaded)
            updateDNA();
        markDirty(AbstractRenderer::AllDirty);
    }

    BackendNode::sceneChangeEvent(e);
}

QHash<QString, ShaderUniform> Shader::activeUniformsForUniformBlock(int blockIndex) const
{
    return m_uniformBlockIndexToShaderUniforms.value(blockIndex);
}

ShaderUniformBlock Shader::uniformBlockForBlockIndex(int blockIndex)
{
    for (int i = 0, m = m_uniformBlocks.size(); i < m; ++i) {
        if (m_uniformBlocks[i].m_index == blockIndex) {
            return m_uniformBlocks[i];
        }
    }
    return ShaderUniformBlock();
}

ShaderUniformBlock Shader::uniformBlockForBlockNameId(int blockNameId)
{
    for (int i = 0, m = m_uniformBlocks.size(); i < m; ++i) {
        if (m_uniformBlocks[i].m_nameId == blockNameId) {
            return m_uniformBlocks[i];
        }
    }
    return ShaderUniformBlock();
}

ShaderUniformBlock Shader::uniformBlockForBlockName(const QString &blockName)
{
    for (int i = 0, m = m_uniformBlocks.size(); i < m; ++i) {
        if (m_uniformBlocks[i].m_name == blockName) {
            return m_uniformBlocks[i];
        }
    }
    return ShaderUniformBlock();
}

ShaderStorageBlock Shader::storageBlockForBlockIndex(int blockIndex)
{
    for (int i = 0, m = m_shaderStorageBlockNames.size(); i < m; ++i) {
        if (m_shaderStorageBlocks[i].m_index == blockIndex)
            return m_shaderStorageBlocks[i];
    }
    return ShaderStorageBlock();
}

ShaderStorageBlock Shader::storageBlockForBlockNameId(int blockNameId)
{
    for (int i = 0, m = m_shaderStorageBlockNames.size(); i < m; ++i) {
        if (m_shaderStorageBlocks[i].m_nameId == blockNameId)
            return m_shaderStorageBlocks[i];
    }
    return ShaderStorageBlock();
}

ShaderStorageBlock Shader::storageBlockForBlockName(const QString &blockName)
{
    for (int i = 0, m = m_shaderStorageBlockNames.size(); i < m; ++i) {
        if (m_shaderStorageBlocks[i].m_name == blockName)
            return m_shaderStorageBlocks[i];
    }
    return ShaderStorageBlock();
}

/*!
 * Must be called with a valid, current QOpenGLContext
 */
QOpenGLShaderProgram *Shader::getOrCreateProgram(GraphicsContext *ctx)
{
    if (!m_isLoaded) {
        delete m_program;
        m_graphicsContext = ctx;
        m_program = createProgram(ctx);
        if (!m_program)
            m_program = createDefaultProgram();
        m_isLoaded = true;
    }
    return m_program;
}

void Shader::updateUniforms(GraphicsContext *ctx, const ShaderParameterPack &pack)
{
    const PackUniformHash values = pack.uniforms();
    const PackUniformHash::const_iterator valueEnd = values.constEnd();

    for (const ShaderUniform &uniform : qAsConst(m_uniforms)) {
        PackUniformHash::const_iterator valueIt = values.constFind(uniform.m_nameId);
        if (valueIt != valueEnd)
            valueIt.value().apply(ctx, uniform);
    }
}

void Shader::setFragOutputs(const QHash<QString, int> &fragOutputs)
{
    {
        QMutexLocker lock(&m_mutex);
        m_fragOutputs = fragOutputs;
    }
    updateDNA();
}

static QOpenGLShader::ShaderType shaderType(QShaderProgram::ShaderType type)
{
    switch (type) {
    case QShaderProgram::Vertex: return QOpenGLShader::Vertex;
    case QShaderProgram::TessellationControl: return QOpenGLShader::TessellationControl;
    case QShaderProgram::TessellationEvaluation: return QOpenGLShader::TessellationEvaluation;
    case QShaderProgram::Geometry: return QOpenGLShader::Geometry;
    case QShaderProgram::Fragment: return QOpenGLShader::Fragment;
    case QShaderProgram::Compute: return QOpenGLShader::Compute;
    default: Q_UNREACHABLE();
    }
}

QOpenGLShaderProgram *Shader::createProgram(GraphicsContext *context)
{
    Q_ASSERT(QOpenGLContext::currentContext());

    // Check if we already have a shader program matching all the shaderCode
    QOpenGLShaderProgram *existingProg = context->containsProgram(m_dna);
    if (existingProg)
        return existingProg;

    // When we arrive at that point, that means that no matching program
    // was found, so we need to load it
    // Scoped pointer so early-returns delete automatically
    QScopedPointer<QOpenGLShaderProgram> p(new QOpenGLShaderProgram);

    for (int i = QShaderProgram::Vertex; i <= QShaderProgram::Compute; ++i) {
        // Compile shaders
        QShaderProgram::ShaderType type = static_cast<const QShaderProgram::ShaderType>(i);
        if (!m_shaderCode[type].isEmpty() && !p->addShaderFromSourceCode(shaderType(type), m_shaderCode[type]))
            qWarning().noquote() << "Failed to compile shader:" << p->log();
    }

    // Call glBindFragDataLocation and link the program
    // Since we are sharing shaders in the backend, we assume that if using custom
    // fragOutputs, they should all be the same for a given shader
    context->bindFragOutputs(p->programId(), m_fragOutputs);
    if (!p->link()) {
        qWarning().noquote() << "Failed to link shader program:" << p->log();
        return nullptr;
    }

    // take from scoped-pointer so it doesn't get deleted
    return p.take();
}

QOpenGLShaderProgram* Shader::createDefaultProgram()
{
    QOpenGLShaderProgram* p = new QOpenGLShaderProgram;
    p->addShaderFromSourceCode(QOpenGLShader::Vertex,
                               "");

    p->addShaderFromSourceCode(QOpenGLShader::Fragment,
                               "");

    p->link();

    return p;
}

void Shader::updateDNA()
{
    uint codeHash = qHash(m_shaderCode[QShaderProgram::Vertex]
            + m_shaderCode[QShaderProgram::TessellationControl]
            + m_shaderCode[QShaderProgram::TessellationEvaluation]
            + m_shaderCode[QShaderProgram::Geometry]
            + m_shaderCode[QShaderProgram::Fragment]
            + m_shaderCode[QShaderProgram::Compute]);

    QMutexLocker locker(&m_mutex);
    uint attachmentHash = 0;
    QHash<QString, int>::const_iterator it = m_fragOutputs.begin();
    QHash<QString, int>::const_iterator end = m_fragOutputs.end();
    while (it != end) {
        attachmentHash += ::qHash(it.value()) + ::qHash(it.key());
        ++it;
    }
    m_dna = codeHash + attachmentHash;
}

void Shader::initializeUniforms(const QVector<ShaderUniform> &uniformsDescription)
{
    m_uniforms = uniformsDescription;
    m_uniformsNames.resize(uniformsDescription.size());
    m_uniformsNamesIds.resize(uniformsDescription.size());
    QHash<QString, ShaderUniform> activeUniformsInDefaultBlock;

    for (int i = 0, m = uniformsDescription.size(); i < m; i++) {
        m_uniformsNames[i] = m_uniforms[i].m_name;
        m_uniforms[i].m_nameId = StringToInt::lookupId(m_uniformsNames[i]);
        m_uniformsNamesIds[i] = m_uniforms[i].m_nameId;
        if (uniformsDescription[i].m_blockIndex == -1) { // Uniform is in default block
            qCDebug(Shaders) << "Active Uniform in Default Block " << uniformsDescription[i].m_name << uniformsDescription[i].m_blockIndex;
            activeUniformsInDefaultBlock.insert(uniformsDescription[i].m_name, uniformsDescription[i]);
        }
    }
    m_uniformBlockIndexToShaderUniforms.insert(-1, activeUniformsInDefaultBlock);
}

void Shader::initializeAttributes(const QVector<ShaderAttribute> &attributesDescription)
{
    m_attributes = attributesDescription;
    m_attributesNames.resize(attributesDescription.size());
    m_attributeNamesIds.resize(attributesDescription.size());
    for (int i = 0, m = attributesDescription.size(); i < m; i++) {
        m_attributesNames[i] = attributesDescription[i].m_name;
        m_attributes[i].m_nameId = StringToInt::lookupId(m_attributesNames[i]);
        m_attributeNamesIds[i] = m_attributes[i].m_nameId;
        qDebug() << m_attributes[i].m_nameId << m_attributeNamesIds[i] << m_attributesNames[i];
        qCDebug(Shaders) << "Active Attribute " << attributesDescription[i].m_name;
    }
}

void Shader::initializeUniformBlocks(const QVector<ShaderUniformBlock> &uniformBlockDescription)
{
    m_uniformBlocks = uniformBlockDescription;
    m_uniformBlockNames.resize(uniformBlockDescription.size());
    m_uniformBlockNamesIds.resize(uniformBlockDescription.size());
    for (int i = 0, m = uniformBlockDescription.size(); i < m; ++i) {
        m_uniformBlockNames[i] = m_uniformBlocks[i].m_name;
        m_uniformBlockNamesIds[i] = StringToInt::lookupId(m_uniformBlockNames[i]);
        m_uniformBlocks[i].m_nameId = m_uniformBlockNamesIds[i];
        qCDebug(Shaders) << "Initializing Uniform Block {" << m_uniformBlockNames[i] << "}";

        // Find all active uniforms for the shader block
        QVector<ShaderUniform>::const_iterator uniformsIt = m_uniforms.begin();
        const QVector<ShaderUniform>::const_iterator uniformsEnd = m_uniforms.end();

        QVector<QString>::const_iterator uniformNamesIt = m_uniformsNames.begin();
        const QVector<QString>::const_iterator uniformNamesEnd = m_attributesNames.end();

        QHash<QString, ShaderUniform> activeUniformsInBlock;

        while (uniformsIt != uniformsEnd && uniformNamesIt != uniformNamesEnd) {
            if (uniformsIt->m_blockIndex == uniformBlockDescription[i].m_index) {
                QString uniformName = *uniformNamesIt;
                if (!m_uniformBlockNames[i].isEmpty() && !uniformName.startsWith(m_uniformBlockNames[i]))
                    uniformName = m_uniformBlockNames[i] + QLatin1Char('.') + *uniformNamesIt;
                activeUniformsInBlock.insert(uniformName, *uniformsIt);
                qCDebug(Shaders) << "Active Uniform Block " << uniformName << " in block " << m_uniformBlockNames[i] << " at index " << uniformsIt->m_blockIndex;
            }
            ++uniformsIt;
            ++uniformNamesIt;
        }
        m_uniformBlockIndexToShaderUniforms.insert(uniformBlockDescription[i].m_index, activeUniformsInBlock);
    }
}

void Shader::initializeShaderStorageBlocks(const QVector<ShaderStorageBlock> &shaderStorageBlockDescription)
{
    m_shaderStorageBlocks = shaderStorageBlockDescription;
    m_shaderStorageBlockNames.resize(shaderStorageBlockDescription.size());
    m_shaderStorageBlockNamesIds.resize(shaderStorageBlockDescription.size());

    for (int i = 0, m = shaderStorageBlockDescription.size(); i < m; ++i) {
        m_shaderStorageBlockNames[i] = m_shaderStorageBlocks[i].m_name;
        m_shaderStorageBlockNamesIds[i] = StringToInt::lookupId(m_shaderStorageBlockNames[i]);
        m_shaderStorageBlocks[i].m_nameId =m_shaderStorageBlockNamesIds[i];
        qCDebug(Shaders) << "Initializing Shader Storage Block {" << m_shaderStorageBlockNames[i] << "}";
    }
}

/*!
   \internal
   Initializes this Shader's state relating to attributes, global block uniforms and
   and named uniform blocks by copying these details from \a other.
*/
void Shader::initialize(const Shader &other)
{
    Q_ASSERT(m_dna == other.m_dna);
    m_program = other.m_program;
    m_uniformsNamesIds = other.m_uniformsNamesIds;
    m_uniformsNames = other.m_uniformsNames;
    m_uniforms = other.m_uniforms;
    m_attributesNames = other.m_attributesNames;
    m_attributeNamesIds = other.m_attributeNamesIds;
    m_attributes = other.m_attributes;
    m_uniformBlockNamesIds = other.m_uniformBlockNamesIds;
    m_uniformBlockNames = other.m_uniformBlockNames;
    m_uniformBlocks = other.m_uniformBlocks;
    m_uniformBlockIndexToShaderUniforms = other.m_uniformBlockIndexToShaderUniforms;
    m_fragOutputs = other.m_fragOutputs;
    m_shaderStorageBlockNamesIds = other.m_shaderStorageBlockNamesIds;
    m_shaderStorageBlockNames = other.m_shaderStorageBlockNames;
    m_shaderStorageBlocks = other.m_shaderStorageBlocks;
    m_isLoaded = other.m_isLoaded;
}

} // namespace Render
} // namespace Qt3DRender

QT_END_NAMESPACE
