/****************************************************************************
**
** Copyright (C) 2016 The Qt Company Ltd.
** Contact: https://www.qt.io/licensing/
**
** This file is part of the examples of the Qt Toolkit.
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

#include "GLWidget.h"

#include <QApplication>

#include <qmath.h>

GLWidget::GLWidget(QWidget *parent) : QOpenGLWidget(parent)
{
    setMinimumSize(300, 250);

    connect(this, &QOpenGLWidget::aboutToCompose, this, &GLWidget::onAboutToCompose);
    connect(this, &QOpenGLWidget::frameSwapped, this, &GLWidget::onFrameSwapped);
    connect(this, &QOpenGLWidget::aboutToResize, this, &GLWidget::onAboutToResize);
    connect(this, &QOpenGLWidget::resized, this, &GLWidget::onResized);

    m_thread = new QThread;
    m_renderer = new Renderer(this);
    m_renderer->moveToThread(m_thread);
    connect(m_thread, &QThread::finished, m_renderer, &QObject::deleteLater);

    connect(this, &GLWidget::renderRequested, m_renderer, &Renderer::render);
    connect(m_renderer, &Renderer::contextWanted, this, &GLWidget::grabContext);

    m_thread->start();
}

GLWidget::~GLWidget()
{
    m_renderer->prepareExit();
    m_thread->quit();
    m_thread->wait();
    delete m_thread;
}

void GLWidget::onAboutToCompose()
{
    // We are on the gui thread here. Composition is about to
    // begin. Wait until the render thread finishes.
    m_renderer->lockRenderer();
}

void GLWidget::onFrameSwapped()
{
    m_renderer->unlockRenderer();
    // Assuming a blocking swap, our animation is driven purely by the
    // vsync in this example.
    emit renderRequested();
}

void GLWidget::onAboutToResize()
{
    // sleep small time before resizing to avoid crash "Cannot make QOpenGLContext current in a different thread"
    QThread::msleep(10);
    m_renderer->lockRenderer();
}

void GLWidget::onResized()
{
    m_renderer->unlockRenderer();
}

void GLWidget::grabContext()
{
    m_renderer->lockRenderer();
    QMutexLocker lock(m_renderer->grabMutex());
    context()->moveToThread(m_thread);
    m_renderer->grabCond()->wakeAll();
    m_renderer->unlockRenderer();
}

void GLWidget::initializeGL()
{
    QOpenGLWidget::initializeGL();
    m_renderer->initGLBuffer();
}

Renderer::Renderer(GLWidget *w) : QObject(nullptr), QOpenGLFunctions()
    , m_glwidget(w)
    , m_exiting(false)
{
}

void Renderer::paintQtLogo()
{
    if (!m_vbo.bind())
    {
        qFatal("Fail to bind GL buffer");
        return;
    }
    m_program.setAttributeBuffer(m_vertexAttr, GL_FLOAT, 0, 3);
    m_program.setAttributeBuffer(m_normalAttr, GL_FLOAT, m_vertices.count() * 3 * sizeof(GLfloat), 3);
    m_vbo.release();

    m_program.enableAttributeArray(m_vertexAttr);
    m_program.enableAttributeArray(m_normalAttr);

    glDrawArrays(GL_TRIANGLES, 0, m_vertices.size());

    m_program.disableAttributeArray(m_normalAttr);
    m_program.disableAttributeArray(m_vertexAttr);
}

// Some OpenGL implementations have serious issues with compiling and linking
// shaders on multiple threads concurrently. Avoid this.
Q_GLOBAL_STATIC(QMutex, initMutex)

void Renderer::initGLBuffer()
{
    initializeOpenGLFunctions();

    const char *vsrc =
        "attribute highp vec4 vertex;\n"
        "attribute mediump vec3 normal;\n"
        "uniform mediump mat4 matrix;\n"
        "varying mediump vec4 color;\n"
        "void main(void)\n"
        "{\n"
        "    vec3 toLight = normalize(vec3(0.0, 0.3, 1.0));\n"
        "    float angle = max(dot(normal, toLight), 0.0);\n"
        "    vec3 col = vec3(0.40, 1.0, 0.0);\n"
        "    color = vec4(col * 0.2 + col * 0.8 * angle, 1.0);\n"
        "    color = clamp(color, 0.0, 1.0);\n"
        "    gl_Position = matrix * vertex;\n"
        "}\n";

    const char *fsrc =
        "varying mediump vec4 color;\n"
        "void main(void)\n"
        "{\n"
        "    gl_FragColor = color;\n"
        "}\n";

    bool isOk = m_program.addCacheableShaderFromSourceCode(QOpenGLShader::Vertex, vsrc);
    isOk &= m_program.addCacheableShaderFromSourceCode(QOpenGLShader::Fragment, fsrc);
    isOk &= m_program.link();
    if (!isOk)
    {
        qFatal("Fail to compile GLSL program");
        return;
    }

    m_vertexAttr = m_program.attributeLocation("vertex");
    m_normalAttr = m_program.attributeLocation("normal");
    m_matrixUniform = m_program.uniformLocation("matrix");

    m_fAngle = 0;
    m_fScale = 1;
    createGeometry();

    if (!m_vbo.create() || !m_vbo.bind())
    {
        qFatal("Fail to bind GL buffer");
        return;
    }
    const int verticesSize = m_vertices.count() * 3 * sizeof(GLfloat);
    m_vbo.allocate(verticesSize * 2);
    m_vbo.write(0, m_vertices.constData(), verticesSize);
    m_vbo.write(verticesSize, m_normals.constData(), verticesSize);

    m_elapsed.start();
}

void Renderer::render()
{
    if (m_exiting)
        return;

    QOpenGLContext *ctx = m_glwidget->context();
    if (!ctx) // QOpenGLWidget not yet initialized
        return;

    // Grab the context.
    m_grabMutex.lock();
    emit contextWanted();
    m_grabCond.wait(&m_grabMutex);
    QMutexLocker lock(&m_renderMutex);
    m_grabMutex.unlock();

    if (m_exiting)
        return;

    if (ctx->thread() != thread() || !ctx->isValid())
    {
        qFatal("Fail to move context in current thread");
        return;
    }

    // Make the context (and an offscreen surface) current for this thread. The
    // QOpenGLWidget's fbo is bound in the context.
    m_glwidget->makeCurrent();

    //qDebug("%p elapsed %lld", QThread::currentThread(), m_elapsed.restart());

    glClearColor(0.1f, 0.2f, 0.2f, 1.0f);
    glClear(GL_COLOR_BUFFER_BIT | GL_DEPTH_BUFFER_BIT);

    glFrontFace(GL_CW);
    glCullFace(GL_FRONT);
    glEnable(GL_CULL_FACE);
    glEnable(GL_DEPTH_TEST);

    QMatrix4x4 modelview;
    modelview.rotate(m_fAngle, 0.0f, 1.0f, 0.0f);
    modelview.rotate(m_fAngle, 1.0f, 0.0f, 0.0f);
    modelview.rotate(m_fAngle, 0.0f, 0.0f, 1.0f);
    modelview.scale(m_fScale);
    modelview.translate(-0.2f, -0.2f, 0.0f);

    if (!m_program.bind())
    {
        qFatal("Fail to bind GLSL program");
        restoreContext();
        return;
    }
    m_program.setUniformValue(m_matrixUniform, modelview);
    paintQtLogo();
    m_program.release();

    glDisable(GL_DEPTH_TEST);
    glDisable(GL_CULL_FACE);

    m_fAngle += 1.0f;

    // Make no context current on this thread and move the QOpenGLWidget's
    // context back to the gui thread.
    restoreContext();

    // Schedule composition. Note that this will use QueuedConnection, meaning
    // that update() will be invoked on the gui thread.
    QMetaObject::invokeMethod(m_glwidget, "update");
}

void Renderer::restoreContext()
{
    QOpenGLContext *ctx = m_glwidget->context();
    if (!ctx) // QOpenGLWidget not yet initialized
        return;

    m_glwidget->doneCurrent();
    ctx->moveToThread(qApp->thread());
}

void Renderer::createGeometry()
{
    m_vertices.clear();
    m_normals.clear();

    qreal x1 = +0.06f;
    qreal y1 = -0.14f;
    qreal x2 = +0.14f;
    qreal y2 = -0.06f;
    qreal x3 = +0.08f;
    qreal y3 = +0.00f;
    qreal x4 = +0.30f;
    qreal y4 = +0.22f;

    quad(x1, y1, x2, y2, y2, x2, y1, x1);
    quad(x3, y3, x4, y4, y4, x4, y3, x3);

    extrude(x1, y1, x2, y2);
    extrude(x2, y2, y2, x2);
    extrude(y2, x2, y1, x1);
    extrude(y1, x1, x1, y1);
    extrude(x3, y3, x4, y4);
    extrude(x4, y4, y4, x4);
    extrude(y4, x4, y3, x3);

    const int NumSectors = 100;
    const qreal sectorAngle = 2 * qreal(M_PI) / NumSectors;

    for (int i = 0; i < NumSectors; ++i) {
        qreal angle = i * sectorAngle;
        qreal x5 = 0.30 * sin(angle);
        qreal y5 = 0.30 * cos(angle);
        qreal x6 = 0.20 * sin(angle);
        qreal y6 = 0.20 * cos(angle);

        angle += sectorAngle;
        qreal x7 = 0.20 * sin(angle);
        qreal y7 = 0.20 * cos(angle);
        qreal x8 = 0.30 * sin(angle);
        qreal y8 = 0.30 * cos(angle);

        quad(x5, y5, x6, y6, x7, y7, x8, y8);

        extrude(x6, y6, x7, y7);
        extrude(x8, y8, x5, y5);
    }

    for (int i = 0;i < m_vertices.size();i++)
        m_vertices[i] *= 2.0f;
}

void Renderer::quad(qreal x1, qreal y1, qreal x2, qreal y2, qreal x3, qreal y3, qreal x4, qreal y4)
{
    m_vertices << QVector3D(x1, y1, -0.05f);
    m_vertices << QVector3D(x2, y2, -0.05f);
    m_vertices << QVector3D(x4, y4, -0.05f);

    m_vertices << QVector3D(x3, y3, -0.05f);
    m_vertices << QVector3D(x4, y4, -0.05f);
    m_vertices << QVector3D(x2, y2, -0.05f);

    QVector3D n = QVector3D::normal
        (QVector3D(x2 - x1, y2 - y1, 0.0f), QVector3D(x4 - x1, y4 - y1, 0.0f));

    m_normals << n;
    m_normals << n;
    m_normals << n;

    m_normals << n;
    m_normals << n;
    m_normals << n;

    m_vertices << QVector3D(x4, y4, 0.05f);
    m_vertices << QVector3D(x2, y2, 0.05f);
    m_vertices << QVector3D(x1, y1, 0.05f);

    m_vertices << QVector3D(x2, y2, 0.05f);
    m_vertices << QVector3D(x4, y4, 0.05f);
    m_vertices << QVector3D(x3, y3, 0.05f);

    n = QVector3D::normal
        (QVector3D(x2 - x4, y2 - y4, 0.0f), QVector3D(x1 - x4, y1 - y4, 0.0f));

    m_normals << n;
    m_normals << n;
    m_normals << n;

    m_normals << n;
    m_normals << n;
    m_normals << n;
}

void Renderer::extrude(qreal x1, qreal y1, qreal x2, qreal y2)
{
    m_vertices << QVector3D(x1, y1, +0.05f);
    m_vertices << QVector3D(x2, y2, +0.05f);
    m_vertices << QVector3D(x1, y1, -0.05f);

    m_vertices << QVector3D(x2, y2, -0.05f);
    m_vertices << QVector3D(x1, y1, -0.05f);
    m_vertices << QVector3D(x2, y2, +0.05f);

    QVector3D n = QVector3D::normal
        (QVector3D(x2 - x1, y2 - y1, 0.0f), QVector3D(0.0f, 0.0f, -0.1f));

    m_normals << n;
    m_normals << n;
    m_normals << n;

    m_normals << n;
    m_normals << n;
    m_normals << n;
}
