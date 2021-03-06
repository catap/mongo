/* commands.cpp
   db "commands" (sent via db.$cmd.findOne(...))
 */

/**
*
*    This program is free software: you can redistribute it and/or  modify
*    it under the terms of the GNU Affero General Public License, version 3,
*    as published by the Free Software Foundation.
*
*    This program is distributed in the hope that it will be useful,
*    but WITHOUT ANY WARRANTY; without even the implied warranty of
*    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
*    GNU Affero General Public License for more details.
*
*    You should have received a copy of the GNU Affero General Public License
*    along with this program.  If not, see <http://www.gnu.org/licenses/>.
*/

#include "stdafx.h"
#include "query.h"
#include "pdfile.h"
#include "jsobj.h"
#include "../util/builder.h"
#include <time.h>
#include "introspect.h"
#include "btree.h"
#include "../util/lruishmap.h"
#include "json.h"
#include "repl.h"
#include "commands.h"

#include "../scripting/engine.h"

namespace mongo {

    const int edebug=0;

    bool dbEval(const char *ns, BSONObj& cmd, BSONObjBuilder& result, string& errmsg) {
        BSONElement e = cmd.firstElement();
        uassert( "eval needs Code" , e.type() == Code || e.type() == CodeWScope || e.type() == String );

        const char *code = 0;
        switch ( e.type() ) {
        case String:
        case Code:
            code = e.valuestr();
            break;
        case CodeWScope:
            code = e.codeWScopeCode();
            break;
        default:
            assert(0);
        }
        assert( code );

        if ( ! globalScriptEngine ) {
            errmsg = "db side execution is disabled";
            return false;
        }

        auto_ptr<Scope> s = globalScriptEngine->getPooledScope( ns );
        ScriptingFunction f = s->createFunction(code);
        if ( f == 0 ) {
            errmsg = (string)"compile failed: " + s->getError();
            return false;
        }
        
        if ( e.type() == CodeWScope )
            s->init( e.codeWScopeScopeData() );
        s->localConnect( database->name.c_str() );

        BSONObj args;
        {
            BSONElement argsElement = cmd.findElement("args");
            if ( argsElement.type() == Array ) {
                args = argsElement.embeddedObject();
                if ( edebug ) {
                    out() << "args:" << args.toString() << endl;
                    out() << "code:\n" << code << endl;
                }
            }
        }

        int res;
        {
            Timer t;
            res = s->invoke(f,args, 10 * 60 * 1000);
            int m = t.millis();
            if ( m > 100 ) {
                out() << "dbeval slow, time: " << dec << m << "ms " << ns << endl;
                if ( m >= 1000 ) log() << code << endl;
                else OCCASIONALLY log() << code << endl;
            }
        }
        if ( res ) {
            result.append("errno", (double) res);
            errmsg = "invoke failed: ";
            errmsg += s->getError();
            return false;
        }
        
        s->append( result , "retval" , "return" );

        return true;
    }

    class CmdEval : public Command {
    public:
        virtual bool slaveOk() {
            return false;
        }
        CmdEval() : Command("$eval") { }
        bool run(const char *ns, BSONObj& cmdObj, string& errmsg, BSONObjBuilder& result, bool fromRepl) {
            return dbEval(ns, cmdObj, result, errmsg);
        }
    } cmdeval;

} // namespace mongo
