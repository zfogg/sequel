"""Host-language and preservation tests for --embedded."""
import ast
import json
import os
from pathlib import Path
import random
import subprocess
import sys
import tempfile
import unittest

BINARY = str(Path(sys.argv.pop(1)).resolve())
SQL = 'SELECT id,name FROM users WHERE id=42;'
PRETTY = 'SELECT\n  id,\n  name\nFROM users\nWHERE\n  id = 42;'


def run(source, language, *options):
    return subprocess.run([BINARY, 'format', '--embedded', '--language', language, *options],
                          input=source.encode() if isinstance(source, str) else source,
                          capture_output=True, timeout=10)


class EmbeddedTests(unittest.TestCase):
    def check(self, source, language, expected=None):
        result = run(source, language)
        self.assertEqual(result.returncode, 0, result.stderr)
        text = result.stdout.decode()
        if expected is not None:
            self.assertEqual(text, expected)
        second = run(text, language)
        self.assertEqual(second.returncode, 0, second.stderr)
        self.assertEqual(second.stdout, result.stdout, 'formatting must be idempotent')
        return text

    def test_names_case_and_aliases(self):
        for language in ['go','GO','Go','c','C','c++','C++','cpp','CPP','python','Python','PYTHON',
                         'javascript','JavaScript','JAVASCRIPT','typescript','TypeScript','TYPESCRIPT',
                         'rust','Rust','RUST','haskell','Haskell','HASKELL','clojure','Clojure','CLOJURE']:
            with self.subTest(language=language):
                self.check('"'+SQL+'"', language, json.dumps(PRETTY))
        self.assertNotEqual(run('"'+SQL+'"', 'scheme').returncode, 0)

    def test_go_strings_and_comments(self):
        fixtures = [
            ('package p\nconst q = `'+SQL+'`\n', 'package p\nconst q = `'+PRETTY+'`\n'),
            ('const q = "'+SQL+'"', 'const q = '+json.dumps(PRETTY)),
            ('const q = "SELECT id," + "name FROM users WHERE id=42;"', 'const q = '+json.dumps(PRETTY)),
            ('// '+SQL+'\nfunc untouched() {}\n', '// '+PRETTY.replace('\n','\n// ')+'\nfunc untouched() {}\n'),
            ('  // '+SQL+'\r\nnext()\r\n', '  // '+PRETTY.replace('\n','\r\n  // ')+'\r\nnext()\r\n'),
            ('/* '+SQL+' */', '/* '+PRETTY+' */'),
        ]
        for source, expected in fixtures:
            with self.subTest(source=source):self.check(source,'go',expected)

    def test_c_adjacent_strings_and_macro(self):
        self.check('const char *q = "SELECT id,"\n "name FROM users WHERE id=42;";', 'c',
                   'const char *q = '+json.dumps(PRETTY)+';')
        self.check('#define QUERY \\\n "SELECT id," \\\n "name FROM users WHERE id=42;"\n', 'c',
                   '#define QUERY \\\n '+json.dumps(PRETTY)+'\n')
        self.check('auto q = R"sql('+SQL+')sql";', 'cpp', 'auto q = R"sql('+PRETTY+')sql";')
        self.check('auto q = R"('+SQL+')";', 'cpp', 'auto q = R"('+PRETTY+')";')

    def test_python_runtime_values(self):
        for quote in ['"', "'", '"""', "'''", 'r"""']:
            sql = "SELECT id,name FROM users WHERE path='C:\\temp' AND label='\u65e5\u672c\u8a9e';"
            # Construct a literal with a valid host encoding, including doubled
            # backslashes when this isn't a raw literal.
            body = sql if quote.startswith('r') else sql.replace('\\','\\\\')
            delimiter = quote[1:] if quote.startswith('r') else quote
            body = body.replace(delimiter, '\\'+delimiter) if len(delimiter)==1 else body
            literal = quote+body+delimiter
            result = self.check('query = '+literal+'\ncount = 7\n','python')
            before = ast.literal_eval(literal)
            after = ast.literal_eval(ast.parse(result).body[0].value)
            a = subprocess.run([BINARY,'format'],input=before.encode(),capture_output=True,check=True).stdout
            b = subprocess.run([BINARY,'format'],input=after.encode(),capture_output=True,check=True).stdout
            self.assertEqual(a,b)
            self.assertTrue(result.endswith('\ncount = 7\n'))
        self.check('# '+SQL+'\n', 'python', '# '+PRETTY.replace('\n','\n# ')+'\n')

    def test_javascript_and_typescript(self):
        for language in ('javascript','typescript'):
            self.check('const q = sql`'+SQL+'`;\n',language,'const q = sql`'+PRETTY+'`;\n')
            self.check('const q = "'+SQL+'";',language,'const q = '+json.dumps(PRETTY)+';')
            source='const re = /"SELECT x,y FROM z"/;\nconst q = "'+SQL+'";'
            self.check(source,language,'const re = /"SELECT x,y FROM z"/;\nconst q = '+json.dumps(PRETTY)+';')
            self.check('const q = `SELECT id,name FROM users WHERE path=\'C:\\\\temp\'`;\n',language)

    def test_rust_raw_strings_lifetimes_and_comments(self):
        for hashes in ('','#','###'):
            self.check('let q = r'+hashes+'"'+SQL+'"'+hashes+';', 'rust',
                       'let q = r'+hashes+'"'+PRETTY+'"'+hashes+';')
        self.check("fn f<'a>(s: &'a str) { let q = \""+SQL+"\"; }", 'rust',
                   "fn f<'a>(s: &'a str) { let q = "+json.dumps(PRETTY)+"; }")
        nested='/* outer /* '+SQL+' */ end */\n'
        self.check(nested,'rust',nested)

    def test_haskell_and_clojure(self):
        self.check('query = "'+SQL+'"\n', 'haskell', 'query = '+json.dumps(PRETTY)+'\n')
        self.check("query' = \""+SQL+'"\n','haskell',"query' = "+json.dumps(PRETTY)+'\n')
        self.check('-- '+SQL+'\n', 'haskell', '-- '+PRETTY.replace('\n','\n-- ')+'\n')
        self.check('{- '+SQL+' -}', 'haskell', '{- '+PRETTY+' -}')
        self.check('q = [sql| '+SQL+' |]', 'haskell', 'q = [sql| '+PRETTY+' |]')
        self.check('q = [other| arbitrary DSL \"SELECT x,y FROM z\" |]', 'haskell', 'q = [other| arbitrary DSL \"SELECT x,y FROM z\" |]')
        self.check('{- outer {- nested -} end -}', 'haskell', '{- outer {- nested -} end -}')
        self.check('(def q "'+SQL+'")\n','clojure','(def q '+json.dumps(PRETTY)+')\n')
        self.check('; '+SQL+'\n','clojure','; '+PRETTY.replace('\n','\n; ')+'\n')
        self.check('(def r #"SELECT x,y FROM z")\n(def q "'+SQL+'")','clojure',
                   '(def r #"SELECT x,y FROM z")\n(def q '+json.dumps(PRETTY)+')')
        self.check('(def c '+chr(92)+'")\n(def q "'+SQL+'")','clojure',
                   '(def c '+chr(92)+'")\n(def q '+json.dumps(PRETTY)+')')

    def test_interpolated_templates_stay_opaque(self):
        templates=[
            'sql`SELECT id,name FROM ${table}`',
            'sql`SELECT ${fn({a: "}", b: `nested ${value}`})} FROM t`',
            'sql`SELECT ${/}/.test(x) ? "yes" : "no"} FROM t`',
            'sql`SELECT ${x / 2} FROM t`',
            'sql`SELECT ${ /* } */ x // }\n} FROM t`',
            'sql`SELECT ${"SELECT id,name FROM users"} FROM t`',
        ]
        for language in ('javascript','typescript'):
            for template in templates:
                source='const dynamic = '+template+';\nconst q = '+json.dumps(SQL)+';'
                expected='const dynamic = '+template+';\nconst q = '+json.dumps(PRETTY)+';'
                with self.subTest(template=template):self.check(source,language,expected)

    def test_no_sql_and_opaque_values(self):
        fixtures=[('go','// Select your favorite color\nconst name="hello"\n'),
                  ('go','const q = "SELECT \\x69d,name FROM users"'),
                  ('c','printf("SELECT id,name FROM %s", table);'),
                  ('c',"printf(\"SELECT id,name FROM t WHERE name='%s'\", name);"),
                  ('python',"q = \"SELECT id,name FROM t WHERE name='%s'\" % name"),
                  ('rust','print!("SELECT id,name FROM {table:10}");'),
                  ('go','q := fmt.Sprintf("SELECT id,name FROM %[1]s", table)'),
                  ('c','snprintf(q,n,"SELECT id,name FROM t_%02d", id);'),
                  ('haskell','q = printf "SELECT id,name FROM t_%*s" width table'),
                  ('python','q = f"SELECT id,name FROM {table}"'),
                  ('python','q = b"SELECT id,name FROM users"'),
                  ('rust','let q = b"SELECT id,name FROM users";'),
                  ('rust','let q = format!("SELECT id,name FROM {table}");'),
                  ('haskell','q = printf "SELECT id,name FROM %s" table'),
                  ('cpp','auto q = fmt::format("SELECT id,name FROM {}", table);'),
                  ('javascript','const re = /["\\/]+SELECT id,name FROM users/;'),
                  ('haskell',"letter = 'a'\nname = \"hello\"\n")]
        for language,source in fixtures:
            with self.subTest(language=language,source=source):self.check(source,language,source)

    def test_unicode_character_literals_and_identifiers(self):
        for language in ('rust','haskell'):
            source="letter = '"+chr(233)+"';\nq = "+json.dumps(SQL)
            self.check(source,language,"letter = '"+chr(233)+"';\nq = "+json.dumps(PRETTY))
        source="fn f<'"+chr(945)+">(x: &'"+chr(945)+" str){let q="+json.dumps(SQL)+";}"
        self.check(source,'rust',source.replace(json.dumps(SQL),json.dumps(PRETTY)))

    def test_multiple_queries_literals_and_comments_in_sql(self):
        sql="-- inside SQL\nSELECT 'a;  b',id FROM users; /* note */ SELECT 2;"
        for language in ('go','c','python','javascript','typescript','rust','haskell','clojure'):
            source='"'+sql.replace('\\','\\\\').replace('"','\\"').replace('\n','\\n')+'"'
            result=self.check(source,language)
            self.assertIn('a;  b',result)
            self.assertIn('-- inside SQL',result)
            self.assertIn('/* note */',result)

    def test_documentation_comment_markers(self):
        for language,marker in [('rust','/// '),('rust','//! '),('cpp','/// '),('clojure',';;; ')]:
            self.check(marker+SQL+'\n', language, marker+PRETTY.replace('\n','\n'+marker)+'\n')
        result=self.check('/** '+SQL+' */','cpp')
        self.assertTrue(result.startswith('/** '))

    def test_crlf_line_continuations(self):
        for language in ('c','python','javascript','haskell'):
            opaque='"SELECT a,'+chr(92)+'\r\n b FROM t"'
            source=opaque+';\r\n'+json.dumps(SQL)
            result=self.check(source,language)
            self.assertTrue(result.startswith(opaque+';\r\n'))
            self.assertTrue(result.endswith(json.dumps(PRETTY)))

    def test_more_host_constructs(self):
        self.check('q = "SELECT id," "name FROM users WHERE id=42;"', 'python', 'q = '+json.dumps(PRETTY))
        self.check('const q = "SELECT id," + "name FROM users WHERE id=42;";', 'javascript', 'const q = '+json.dumps(PRETTY)+';')
        self.check('q = "SELECT id," ++ "name FROM users WHERE id=42;"', 'haskell', 'q = '+json.dumps(PRETTY))
        self.check('/*\n * '+SQL+'\n */', 'c')
        source='q = br#"'+SQL+'"#;'
        self.check(source,'rust',source)
        self.check("type T = 'Just Int\nq = "+json.dumps(SQL), 'haskell', "type T = 'Just Int\nq = "+json.dumps(PRETTY))
        for language in ('go','c','rust','haskell','clojure','python','javascript','typescript'):
            source=json.dumps("SELECT 'unterminated")
            self.check(source,language,source)
        self.check('// SELECT id,name\n// FROM users\n// WHERE id=42;\n// normal prose\n', 'go',
                   '// '+PRETTY.replace('\n','\n// ')+'\n// normal prose\n')

    def test_dialects_and_parameters(self):
        queries={
            'postgresql': "SELECT id,payload::jsonb FROM t WHERE id=$1 AND name=$name;",
            'sqlite': "SELECT [name],?1,:name,@name FROM t;",
            'mysql': "SELECT `id`,'a  b',? FROM users WHERE id=?;",
            'duckdb': "SELECT * EXCLUDE(secret),[1,2,3] FROM t;",
            'clickhouse': "SELECT id,{value:Nullable(Int64)} FROM t FINAL WHERE id={id:UInt64};",
            'generic': "SELECT id,name FROM t WHERE id=?;",
        }
        for dialect,sql in queries.items():
            for language in ('go','c','python','rust','haskell','clojure','javascript','typescript'):
                with self.subTest(dialect=dialect,language=language):
                    result=run(json.dumps(sql),language,'--dialect',dialect)
                    self.assertEqual(result.returncode,0,result.stderr)
                    value=json.loads(result.stdout)
                    self.assertNotEqual(value,sql)
                    expected=subprocess.run([BINARY,'format','--dialect',dialect],input=sql.encode(),capture_output=True,check=True).stdout.decode()
                    self.assertEqual(value,expected)
        result=self.check('// SELECT a,b FROM @@table WHERE id=@id\n', 'go')
        self.assertIn('@@table',result)
        self.assertIn('@id',result)

    def test_limits_and_hostile_inputs(self):
        result=run(b'x'*(1024*1024+1),'go')
        self.assertNotEqual(result.returncode,0)
        result=run('r'+'#'*256+'"SELECT 1"'+'#'*256,'rust')
        self.assertNotEqual(result.returncode,0)
        rng=random.Random(314159)
        for language in ('go','c','python','rust','haskell','clojure','javascript','typescript'):
            for _ in range(12):
                source=''.join(rng.choice('ab 01/*{};#$@\\\"\'`\n') for _ in range(180))
                result=run(source,language)
                self.assertIn(result.returncode,(0,1))
                if result.returncode:self.assertEqual(result.stdout,b'')

    def test_extensions_stdout_edit_and_unicode_paths(self):
        for extension in ['go','C','cpp','py','js','ts','rs','hs','clj']:
            with self.subTest(extension=extension),tempfile.TemporaryDirectory() as folder:
                path=Path(folder)/('\u65e5\u672c caf\xe9.'+extension)
                source='"'+SQL+'"';path.write_text(source)
                result=subprocess.run([BINARY,'format','--embedded',str(path)],capture_output=True,check=True)
                self.assertEqual(result.stdout.decode(),json.dumps(PRETTY))
                self.assertEqual(path.read_text(),source)
                subprocess.run([BINARY,'format','--embedded','--edit-file',str(path)],capture_output=True,check=True)
                self.assertEqual(path.read_bytes(),result.stdout)
                self.assertEqual(list(Path(folder).glob('*.tmp')),[])

    @unittest.skipIf(os.name=='nt','POSIX pseudo-terminal required')
    def test_embedded_interactive_input_never_waits(self):
        import pty
        master,slave=pty.openpty()
        try:
            result=subprocess.run([BINARY,'format','--embedded','--language','go'],stdin=slave,capture_output=True,timeout=3)
            self.assertNotEqual(result.returncode,0)
            self.assertEqual(result.stdout,b'')
        finally:
            os.close(master);os.close(slave)

    def test_failures_do_not_modify_files(self):
        for language,source in [('go','"unterminated'),('c','/* unclosed'),('python','"""unclosed'),
                                ('rust','r###"unclosed'),('haskell','{- unclosed'),
                                ('javascript','const q = `SELECT ${value}'),('go','"SELECT 1"\0'),('go',b'\xff')]:
            with self.subTest(language=language),tempfile.TemporaryDirectory() as folder:
                path=Path(folder)/'source.txt';before=source.encode() if isinstance(source,str) else source;path.write_bytes(before)
                result=run('',language,'--edit-file',str(path))
                self.assertNotEqual(result.returncode,0)
                self.assertEqual(result.stdout,b'')
                self.assertEqual(path.read_bytes(),before)
        self.assertNotEqual(run('', 'go', '--dialect', 'unknown').returncode,0)
        result=subprocess.run([BINARY,'sanitize','--embedded','--language','go'],input=b'"SELECT 1"',capture_output=True)
        self.assertNotEqual(result.returncode,0)

    def test_static_like_patterns_are_not_format_directives(self):
        sql="SELECT id,name FROM users WHERE name LIKE '%secret%' AND path LIKE '%02d%';"
        expected=subprocess.run([BINARY,'format'],input=sql.encode(),capture_output=True,check=True).stdout.decode()
        for language in ('go','c','python','rust','haskell','clojure','javascript','typescript'):
            self.check(json.dumps(sql),language,json.dumps(expected))

    def test_seeded_mixed_content(self):
        rng=random.Random(271828)
        for _ in range(60):
            number=rng.randrange(100000)
            sql=f"SELECT a,b FROM table_{number} WHERE id={number} AND note='keep  spaces';"
            for language in ('go','c','rust','haskell','clojure','python','javascript','typescript'):
                source='"non SQL, leave   alone";\n'+json.dumps(sql)+';\n"untouched";\n'
                result=self.check(source,language)
                self.assertTrue(result.startswith('"non SQL, leave   alone";\n'))
                self.assertTrue(result.endswith('\n"untouched";\n'))
                self.assertIn("keep  spaces",result)
                self.assertIn(chr(92)+"n",result)


if __name__=='__main__':unittest.main()
