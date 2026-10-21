#pragma once

#include <QString>

class Workspace;

// The workspace new users start with.
namespace Templates
{
// The pages a fresh workspace starts with. Returns the welcome page.
QString createSampleWorkspace(Workspace &ws);
}
