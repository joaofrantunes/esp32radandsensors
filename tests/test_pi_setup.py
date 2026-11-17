import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from pi_setup.__main__ import ROOT, TABLES, SetupError, atomic_replace, ddl_statements, merge_flow, read_flow
from pi_setup import __main__ as cli


class SetupTests(unittest.TestCase):
    def setUp(self):
        self.target = read_flow(ROOT / 'node-red/flows.json')
        self.original = [dict(n) for n in self.target if not n['id'].startswith('a160')]
        self.unrelated = {'id':'other-project','type':'tab','label':'Other project'}

    def test_preserve_configs_and_other_projects(self):
        mqtt = next(n for n in self.original if n['type']=='mqtt-broker')
        mqtt['broker'] = '192.0.2.10'; mqtt['port']='1884'
        mysql = next(n for n in self.original if n['type']=='Stackhero-MySQL-Server')
        mysql['host'] = '192.0.2.11'; mysql['database']='example'
        merged = merge_flow(self.original+[self.unrelated],self.target)
        self.assertEqual(next(n for n in merged if n['id']==mqtt['id']),mqtt)
        self.assertEqual(next(n for n in merged if n['id']==mysql['id']),mysql)
        self.assertIn(self.unrelated,merged)
        self.assertEqual(len(merged),len(self.target)+1)
        self.assertEqual(merge_flow(merged,self.target),merged)

    def test_refuse_unknown_legacy(self):
        with self.assertRaises(SetupError): merge_flow([self.unrelated],self.target)

    def test_refuse_duplicate_topics(self):
        for topic in ['esp32/cpm','esp32/#','+/cpm','#']:
            other={'id':'unrelated-mqtt','type':'mqtt in','z':'other-project','topic':topic}
            with self.assertRaises(SetupError): merge_flow(self.original+[other],self.target)

    def test_refuse_custom_project_node(self):
        anchor=next(n['id'] for n in self.original if n['type']=='tab')
        with self.assertRaises(SetupError): merge_flow(self.original+[{'id':'custom','type':'debug','z':anchor}],self.target)

    def test_refuse_custom_mqtt_topic(self):
        next(n for n in self.original if n['type']=='mqtt in')['topic']='lite/temperature'
        with self.assertRaises(SetupError): merge_flow(self.original,self.target)

    def test_install_separate_project(self):
        self.assertEqual(len(merge_flow([self.unrelated],self.target,fresh=True)),len(self.target)+1)
        with self.assertRaises(SetupError): merge_flow(self.original,self.target,fresh=True)

    def test_additive_schema(self):
        schema=(ROOT/'database/schema.sql').read_text()
        self.assertEqual(tuple(ddl_statements(schema)),TABLES)
        self.assertEqual(len(ddl_statements(schema,fresh=True)),20)
        for statement in ddl_statements(schema).values():
            self.assertTrue(statement.startswith('CREATE TABLE IF NOT EXISTS'))
            self.assertNotRegex(statement,r'\b(DROP|DELETE|ALTER|TRUNCATE)\b')

    def test_atomic_file_and_invalid_ids(self):
        with tempfile.TemporaryDirectory() as tmp:
            p=Path(tmp)/'flows.json'; p.write_text('[]'); metadata=p.stat()
            atomic_replace(p,json.dumps(self.target).encode(),metadata)
            self.assertEqual(read_flow(p),self.target)
            p.write_text('[{"id":"one"},{"id":"one"}]')
            with self.assertRaises(SetupError): read_flow(p)

    def test_plan_and_service_failure_rollback(self):
        with tempfile.TemporaryDirectory() as tmp:
            directory=Path(tmp); path=directory/'flows.json'; credential=directory/'flows_cred.json'
            original=json.dumps(self.original).encode(); path.write_bytes(original); credential.write_bytes(b'private')
            for p in cli.PALETTES:
                package=directory/'node_modules'/p/'package.json'; package.parent.mkdir(parents=True); package.write_text('{}')
            commands=[]; created=set(); starts=0
            def fake_run(args, *, data=None, output=None):
                nonlocal starts
                commands.append(args)
                if args[0]=='dump': output.write(b'-- test backup'); return ''
                if args[0]=='db': created.update(TABLES); return ''
                if args[1]=='is-active': return 'active\n'
                if args[1]=='start':
                    starts += 1
                    if starts==1: raise SetupError('mock service start failed')
                return ''
            with patch.object(cli,'discover_flow',return_value=(path,'pi')), \
                 patch.object(cli,'check_database',side_effect=lambda *a: (['db'],set(created))), \
                 patch.object(cli,'db_args',return_value=['dump']), \
                 patch.object(cli,'run',side_effect=fake_run), \
                 patch.object(cli.os,'geteuid',return_value=0):
                with patch.object(cli.sys,'argv',['pi_setup','plan']): cli.main()
                self.assertEqual(commands,[]); self.assertEqual(path.read_bytes(),original)
                with patch.object(cli.sys,'argv',['pi_setup','update','--yes','--backup-dir',str(directory/'backup')]):
                    with self.assertRaises(SetupError): cli.main()
                self.assertEqual(path.read_bytes(),original)
                self.assertEqual(credential.read_bytes(),b'private')
                self.assertEqual(starts,2)
                self.assertEqual(created,set(TABLES))


if __name__=='__main__': unittest.main()
