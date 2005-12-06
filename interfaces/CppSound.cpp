/**
 * C S O U N D   V S T
 *
 * A VST plugin version of Csound, with Python scripting.
 *
 * L I C E N S E
 *
 * This software is free software; you can redistribute it and/or
 * modify it under the terms of the GNU Lesser General Public
 * License as published by the Free Software Foundation; either
 * version 2.1 of the License, or (at your option) any later version.
 *
 * This software is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
 * Lesser General Public License for more details.
 *
 * You should have received a copy of the GNU Lesser General Public
 * License along with this software; if not, write to the Free Software
 * Foundation, Inc., 59 Temple Place, Suite 330, Boston, MA  02111-1307  USA
 */
#include "CppSound.hpp"

#include <cstdio>
#include <cstring>

CppSound::CppSound() : Csound(),
                       go(false),
                       isCompiled(false),
                       isPerforming(false),
                       spoutSize(0)
{
  SetHostData((CSOUND *)0);
}

CppSound::~CppSound()
{
}

int CppSound::compile(int argc, char **argv_)
{
  Message("BEGAN CppSound::compile(%d, %p)...\n", argc, argv_);
  go = false;
  int returnValue = Compile(argc, argv_);
  spoutSize = GetKsmps() * GetNchnls() * sizeof(MYFLT);
  if(returnValue)
    {
      isCompiled = false;
    }
  else
    {
      const char *outfilename = GetOutputFileName();
      if (outfilename) {
        renderedSoundfile = outfilename;
      }
      isCompiled = true;
      go = true;
    }
  Message("ENDED CppSound::compile.\n");
  return returnValue;
}

int CppSound::compile()
{
  Message("BEGAN CppSound::compile()...\n");
  if(getCommand().length() <= 0)
    {
      Message("No Csound command.\n");
      return 0;
    }
  scatterArgs(getCommand(), const_cast< std::vector<std::string> & >(args), const_cast< std::vector<char *> &>(argv));
  int returnValue = compile(args.size(), &argv.front());
  Message("ENDED CppSound::compile.\n");
  return returnValue;
}

int CppSound::perform(int argc, char **argv_)
{
  double beganAt = double(clock()) / double(CLOCKS_PER_SEC);
  isCompiled = false;
  go = false;
  Message("BEGAN CppSound::perform(%d, %p)...\n", argc, argv_);
  if(argc <= 0)
    {
      Message("ENDED CppSound::perform without compiling or performing.\n");
      return 0;
    }
  int result = compile(argc, argv_);
  if(result == -1)
    {
      return result;
    }
  for(result = 0; (result == 0) && go; )
    {
      result = PerformKsmps();
    }
  cleanup();
  double endedAt = double(clock()) / double(CLOCKS_PER_SEC);
  double elapsed = endedAt - beganAt;
  Message("Elapsed time = %f seconds.\n", elapsed);
  Message("ENDED CppSound::perform.\n");
  isCompiled = false;
  isPerforming = false;
  return 1;
}

int CppSound::perform()
{
  int returnValue = 0;
  std::string command = getCommand();
  if(command.find("-") == 0)
    {
      const char *argv_[] = {"csound", getFilename().c_str(), 0};
      returnValue = perform(2, (char **)argv_);
    }
  else
    {
      scatterArgs(command, const_cast< std::vector<std::string> & >(args), const_cast< std::vector<char *> &>(argv));
      returnValue = perform(args.size(), &argv.front());
    }
  return returnValue;
}

void CppSound::stop()
{
  Message("RECEIVED CppSound::stop...\n");
  isCompiled = false;
  isPerforming = false;
  go = false;
}

CSOUND *CppSound::getCsound()
{
  return csound;
}

int CppSound::performKsmps(bool absolute)
{
  if(absolute){
    return PerformKsmpsAbsolute();
  }
  else {
    return PerformKsmps();
  }
}

void CppSound::cleanup()
{
  Cleanup();
  Reset();
}

size_t CppSound::getSpoutSize() const
{
  return spoutSize;
}

std::string CppSound::getOutputSoundfileName() const
{
  return renderedSoundfile;
}

void CppSound::inputMessage(const char *istatement)
{
  InputMessage(istatement);
}

void CppSound::write(const char *text)
{
  Message("%s", text);
}

long CppSound::getThis()
{
  return (long) this;
}

CsoundFile *CppSound::getCsoundFile()
{
  return dynamic_cast<CsoundFile *>(this);
}

bool CppSound::getIsCompiled() const
{
  return isCompiled;
}

void CppSound::setIsPerforming(bool isPerforming)
{
  this->isPerforming = isPerforming;
}

bool CppSound::getIsPerforming() const
{
  return isPerforming;
}

bool CppSound::getIsGo()
{
  if(csound) {
    if(GetSpin() && GetSpout()) {
      return go;
    }
  }
  return false;
}

extern "C"
{
  int PyRun_SimpleString(const char *string);
}

static void pythonMessageCallback(CSOUND *csound, int attr, const char *format, va_list valist)
{
  static char buffer[0x1000];
  static char buffer1[0x1000];
  vsprintf(buffer, format, valist);
  static std::string actualBuffer;
  static std::string lineBuffer;
  actualBuffer.append(buffer);
  size_t position = 0;
  while((position = actualBuffer.find("\n")) != std::string::npos)
    {
      lineBuffer = actualBuffer.substr(0, position);
      actualBuffer.erase(0, position + 1);
#ifndef MSVC
      actualBuffer.clear();
#endif
      sprintf(buffer1, "print '''%s'''", lineBuffer.c_str());
      PyRun_SimpleString(buffer1);
    }
}

void CppSound::setPythonMessageCallback()
{
  SetMessageCallback(pythonMessageCallback);
}

