#include "errnoexception.h"

#include "../engine_p.h"

using namespace NodeQml;

DEFINE_OBJECT_VTABLE(ErrnoExceptionObject);

Heap::ErrnoExceptionObject::ErrnoExceptionObject(QV4::ExecutionEngine *v4, const QString &message,
                                                 int errorNo, const QString &syscall,
                                                 const QString &path) :
    QV4::Heap::ErrorObject(EnginePrivate::get(v4)->errnoExceptionClass, message)
{
    QV4::Scope scope(v4);
    QV4::ScopedObject self(scope, this);
    QV4::ScopedValue v(scope);

    self->defineDefaultProperty(QStringLiteral("errno"), (v = QV4::Encode(errorNo)));

    if (!syscall.isEmpty())
        self->defineDefaultProperty(QStringLiteral("syscall"), (v = v4->newString(syscall)));

    if (!path.isEmpty())
        self->defineDefaultProperty(QStringLiteral("path"), (v = v4->newString(path)));
}
