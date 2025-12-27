#pragma once

#include <cstdlib>
#include <cstring>

namespace agus
{
inline bool IsEnvEnabled(char const * env)
{
  if (env == nullptr)
    return false;

  // Treat explicit false-y strings as disabled.
  if (::strcmp(env, "0") == 0)
    return false;
  if (::strcmp(env, "false") == 0)
    return false;
  if (::strcmp(env, "FALSE") == 0)
    return false;

  return true;
}

inline bool IsAgusVerboseEnabled()
{
  return IsEnvEnabled(std::getenv("AGUS_VERBOSE_LOG")) || IsEnvEnabled(std::getenv("AGUS_PROFILE"));
}
}  // namespace agus
