#include "engine.h"
#include "engine_p.h"

#include "globalextensions.h"
#include "moduleobject.h"
#include "modules/filesystem.h"
#include "modules/os.h"
#include "modules/path.h"
#include "modules/util.h"
#include "types/buffer.h"
#include "types/errnoexception.h"

#include <QCoreApplication>
#include <QFileInfo>
#include <QLoggingCategory>
#include <QQmlEngine>
#include <QTimerEvent>

#include <private/qjsvalue_p.h>
#include <private/qv4engine_p.h>
#include <private/qv8engine_p.h>

namespace {
const QLoggingCategory logCategory("nodeqml.core");
}

using namespace NodeQml;

class NextTickEvent : public QEvent
{
public:
    NextTickEvent(const QV4::PersistentValue &callback) :
        QEvent(NextTickEvent::eventType()),
        m_callback(callback)
    {

    }

    QV4::PersistentValue callback() const
    {
        return m_callback;
    }

    static QEvent::Type eventType()
    {
        if (m_type == QEvent::None)
            m_type = static_cast<QEvent::Type>(QEvent::registerEventType());
        return m_type;
    }

private:
    static QEvent::Type m_type;
    QV4::PersistentValue m_callback;
};

QEvent::Type NextTickEvent::m_type = QEvent::None;

Engine::Engine(QQmlEngine *qmlEngine, QObject *parent) :
    QObject(parent),
    d_ptr(new EnginePrivate(qmlEngine, this))
{

}

QJSValue Engine::require(const QString &id)
{
    Q_D(Engine);
    return new QJSValuePrivate(d->require(id));
}

bool Engine::hasException() const
{
    Q_D(const Engine);
    return d->m_v4->hasException;
}

QHash<QV4::ExecutionEngine *, EnginePrivate*> EnginePrivate::m_nodeEngines;

EnginePrivate *EnginePrivate::get(QV4::ExecutionEngine *v4)
{
    return m_nodeEngines.value(v4);
}

EnginePrivate::EnginePrivate(QQmlEngine *qmlEngine, Engine *engine) :
    QObject(engine),
    q_ptr(engine),
    m_qmlEngine(qmlEngine),
    m_v4(QV8Engine::getV4(qmlEngine))
{
    /// TODO: Mutex
    m_nodeEngines.insert(m_v4, this);

    NodeQml::GlobalExtensions::init(m_qmlEngine);
    registerTypes();
    /// TODO: Core modules should not be loaded unless required
    registerModules();
}

EnginePrivate::~EnginePrivate()
{
    m_nodeEngines.remove(m_v4);
}

bool EnginePrivate::hasNativeModule(const QString &id) const
{
    return m_coreModules.contains(id);
}

QV4::Object *EnginePrivate::nativeModule(const QString &id) const
{
    QV4::Scope scope(m_v4);
    QV4::Scoped<QV4::Object> module(scope, m_coreModules.value(id));
    return module.getPointer();
}

void EnginePrivate::cacheModule(const QString &id, ModuleObject *module)
{
    Q_ASSERT(!m_cachedModules.contains(id));
    m_cachedModules.insert(id, module);
}

bool EnginePrivate::hasCachedModule(const QString &id) const
{
    return m_cachedModules.contains(id);
}

QV4::Object *EnginePrivate::cachedModule(const QString &id) const
{
    return m_cachedModules.value(id);
}

QV4::ReturnedValue EnginePrivate::require(const QString &id, QV4::ExecutionContext *ctx)
{
    if (!ctx)
        ctx = m_v4->currentContext();

    return ModuleObject::require(ctx, id)->asReturnedValue();
}

QV4::ReturnedValue EnginePrivate::setTimeout(QV4::CallContext *ctx)
{
    NODE_CTX_CALLDATA(ctx);
    if (callData->argc < 2)
        return m_v4->throwError("setTimeout: missing arguments");

    QV4::Scope scope(ctx);
    QV4::ScopedFunctionObject cb(scope, callData->args[0].asFunctionObject());

    if (!cb)
        return m_v4->throwTypeError("setTimeout: callback must be a function");

    if (!callData->args[1].isNumber())
        return m_v4->throwTypeError("setTimeout: timeout must be an integer");

    int delay = callData->args[1].toInt32();
    if (delay <= 0)
        delay = 1;

    const int timerId = startTimer(delay, Qt::PreciseTimer);
    if (!timerId)
        return m_v4->throwError("setTimeout: cannot start timer");

    m_timeoutCallbacks.insert(timerId, cb.asReturnedValue());

    /// TODO: Return an object similar to Node's
    return QV4::Encode(timerId);
}

QV4::ReturnedValue EnginePrivate::clearTimeout(QV4::CallContext *ctx)
{
    NODE_CTX_CALLDATA(ctx);
    if (callData->argc < 1)
        return m_v4->throwError("clearTimeout: missing arguments");

    if (!callData->args[0].isNumber())
        return m_v4->throwTypeError("clearTimeout: timeout must be an integer (at the moment)");

    const int timerId = callData->args[0].toInt32();
    if (m_timeoutCallbacks.contains(timerId)) {
        killTimer(timerId);
        m_timeoutCallbacks.remove(timerId);
    }

    return QV4::Encode::undefined();
}

QV4::ReturnedValue EnginePrivate::setInterval(QV4::CallContext *ctx)
{
    NODE_CTX_CALLDATA(ctx);
    if (callData->argc < 2)
        return m_v4->throwError("setInterval: missing arguments");

    QV4::Scope scope(ctx);
    QV4::ScopedFunctionObject cb(scope, callData->args[0].asFunctionObject());

    if (!cb)
        return m_v4->throwTypeError("setInterval: callback must be a function");

    if (!callData->args[1].isNumber())
        return m_v4->throwTypeError("setInterval: timeout must be an integer");

    int delay = callData->args[1].toInt32();
    if (delay <= 0)
        delay = 1;

    const int timerId = startTimer(delay, Qt::PreciseTimer);
    if (!timerId)
        return m_v4->throwError("setInterval: cannot start timer");

    m_intervalCallbacks.insert(timerId, cb.asReturnedValue());

    /// TODO: Return an object similar to Node's
    return QV4::Encode(timerId);
}

QV4::ReturnedValue EnginePrivate::clearInterval(QV4::CallContext *ctx)
{
    NODE_CTX_CALLDATA(ctx);
    if (callData->argc < 1)
        return m_v4->throwError("clearInterval: missing arguments");

    if (!callData->args[0].isNumber())
        return m_v4->throwTypeError("clearInterval: timeout must be an integer (at the moment)");

    const int timerId = callData->args[0].toInt32();
    if (!m_intervalCallbacks.contains(timerId)) {
        killTimer(timerId);
        m_intervalCallbacks.remove(timerId);
    }

    return QV4::Encode::undefined();
}

QV4::ReturnedValue EnginePrivate::nextTick(QV4::CallContext *ctx)
{
    NODE_CTX_CALLDATA(ctx);
    if (!callData->argc)
        return m_v4->throwError("setInterval: missing arguments");

    QV4::Scope scope(ctx);
    QV4::ScopedFunctionObject cb(scope, callData->args[0].asFunctionObject());

    if (!cb)
        return m_v4->throwTypeError("setInterval: callback must be a function");

    NextTickEvent *e = new NextTickEvent(cb.asReturnedValue());
    qApp->postEvent(this, e, INT_MAX);

    return QV4::Encode::undefined();
}

QV4::ReturnedValue EnginePrivate::throwErrnoException(int errorNo, const QString &syscall)
{
    const QString message = QString::fromLocal8Bit(strerror(errorNo));

    QV4::Scope scope(m_v4);
    QV4::ScopedObject o(scope, m_v4->memoryManager->alloc<ErrnoExceptionObject>(m_v4, message, errorNo, syscall));
    return m_v4->throwError(o);
}

void EnginePrivate::customEvent(QEvent *event)
{
    if (event->type() != NextTickEvent::eventType()) {
        QObject::customEvent(event);
        return;
    }

    event->accept();

    NextTickEvent *e = reinterpret_cast<NextTickEvent *>(event);
    QV4::Scope scope(m_v4);
    QV4::ScopedFunctionObject cb(scope, e->callback());
    QV4::ScopedCallData callData(scope, 0);
    callData->thisObject = m_v4->globalObject->asReturnedValue();
    cb->call(callData);
}

void EnginePrivate::timerEvent(QTimerEvent *event)
{
    const int timerId = event->timerId();

    QV4::Scope scope(m_v4);
    QV4::ScopedFunctionObject cb(scope);

    if (m_timeoutCallbacks.contains(timerId)) {
        killTimer(timerId);
        cb = m_timeoutCallbacks.take(timerId);
    } else if (m_intervalCallbacks.contains(timerId)) {
        cb = m_intervalCallbacks.value(timerId);
    } else {
        return;
    }

    event->accept();

    QV4::ScopedCallData callData(scope, 0);
    callData->thisObject = m_v4->globalObject->asReturnedValue();
    QV4::SimpleScriptFunction::call(cb, callData);
}

void EnginePrivate::registerTypes()
{
    QV4::MemoryManager::GCBlocker gcBlocker(m_v4->memoryManager);

    QV4::Scope scope(m_v4);

    QV4::Scoped<ErrnoExceptionPrototype> errnoExceptionPrototype(scope, m_v4->memoryManager->alloc<ErrnoExceptionPrototype>(m_v4->errorClass));
    errnoExceptionClass = QV4::InternalClass::create(m_v4, ErrnoExceptionObject::staticVTable(), errnoExceptionPrototype);

    bufferCtor = QV4::Value::fromHeapObject(m_v4->memoryManager->alloc<BufferCtor>(m_v4->rootContext));
    QV4::Scoped<BufferPrototype> bufferPrototype(scope, m_v4->memoryManager->alloc<BufferPrototype>(m_v4->objectClass));
    bufferPrototype->init(m_v4, bufferCtor.asObject());
    bufferClass = QV4::InternalClass::create(m_v4, BufferObject::staticVTable(), bufferPrototype);

    m_v4->globalObject->defineDefaultProperty(QStringLiteral("Buffer"), bufferCtor);
    m_v4->globalObject->defineDefaultProperty(QStringLiteral("SlowBuffer"), bufferCtor);
}

void EnginePrivate::registerModules()
{
    m_coreModules.insert(QStringLiteral("fs"), m_v4->memoryManager->alloc<FileSystemModule>(m_v4)->asReturnedValue());
    m_coreModules.insert(QStringLiteral("os"), m_v4->memoryManager->alloc<OsModule>(m_v4)->asReturnedValue());
    m_coreModules.insert(QStringLiteral("path"), m_v4->memoryManager->alloc<PathModule>(m_v4)->asReturnedValue());
    m_coreModules.insert(QStringLiteral("util"), m_v4->memoryManager->alloc<UtilModule>(m_v4)->asReturnedValue());
}
