package print.engine;

import java.io.FileWriter;
import java.io.IOException;
import java.util.ArrayList;
import java.util.HashMap;
import java.util.HashSet;

import print.parse.ParserType;
import print.engine.Engine;
import print.engine.RefinedAccessTimesEngine;
import print.event.Event;
import print.event.EventType;
import print.event.Thread;
import print.event.Lock;
import print.event.Variable;

public class ZipTrackPrintEngine extends Engine<Event> {

	long event_index;
	HashMap<Thread, HashMap<Variable, Long>> readMap;
	HashMap<Thread, HashMap<Variable, Long>> writeMap;
	HashMap<Thread, HashMap<Lock, Long>> acquireMap;
	HashMap<Thread, HashMap<Lock, Long>> releaseMap;
	HashMap<Thread, HashMap<Thread, Long>> forkMap;
	HashMap<Thread, HashMap<Thread, Long>> joinMap;
	HashMap<Thread, Long> beginMap;
	HashMap<Thread, Long> endMap;
	HashMap<Thread, Long> txnDepthMap;	// for handling nested transactions
	ArrayList<String> eventStrings;

	private HashMap<String, HashSet<String>> variableToThreadSet;
	private HashMap<String, HashSet<String>> lockToThreadSet;
	private HashSet<String> threadSet;
	private HashSet<String> variablesWritten;

	private boolean removeThreadLocalEvents;

	private String path_to_map_file;

	public ZipTrackPrintEngine(ParserType pType, String trace_folder, String path_to_map_file, boolean th) {
		super(pType);
		initializeReader(trace_folder);
		handlerEvent = new Event();
		event_index = 0;

		this.removeThreadLocalEvents = th;

		readMap = new HashMap<Thread, HashMap<Variable, Long>> ();
		writeMap = new HashMap<Thread, HashMap<Variable, Long>> ();
		acquireMap = new HashMap<Thread, HashMap<Lock, Long>> ();
		releaseMap = new HashMap<Thread, HashMap<Lock, Long>> ();
		forkMap = new HashMap<Thread, HashMap<Thread, Long>> ();
		joinMap = new HashMap<Thread, HashMap<Thread, Long>> ();
		beginMap = new HashMap<Thread, Long> ();
		endMap = new HashMap<Thread, Long> ();
		txnDepthMap = new HashMap<Thread, Long> ();
		eventStrings = new ArrayList<String> ();

		RefinedAccessTimesEngine accessTimesEngine = new RefinedAccessTimesEngine(pType, trace_folder);
		accessTimesEngine.computeLastAccessTimes();

		this.variableToThreadSet = accessTimesEngine.variableToThreadSet;
		this.lockToThreadSet = accessTimesEngine.lockToThreadSet;
		this.threadSet = accessTimesEngine.threadSet;
		this.variablesWritten = accessTimesEngine.variablesWritten;

		this.path_to_map_file = path_to_map_file;
	}

	public void analyzeTrace() {
		if(this.parserType.isRV()){
			analyzeTraceRV();
		}
		else if(this.parserType.isSTD()){
			analyzeTraceSTD();
		}
		else if(this.parserType.isRR()){
			analyzeTraceRR();
		}
		dumpMap();
	}

	private void analyzeTraceRV() {
		if(rvParser.pathListNotNull()){
			while(rvParser.hasNext()){
				rvParser.getNextEvent(handlerEvent);
				if(! skipEvent(handlerEvent)){
					processEvent(handlerEvent);
				}
			}
		}
	}
	
	public void analyzeTraceSTD() {
		while (stdParser.hasNext()) {
			stdParser.getNextEvent(handlerEvent);

			Thread t = handlerEvent.getThread();

			if (!txnDepthMap.containsKey(t)) {
				txnDepthMap.put(t, 0L);
			}

			if (handlerEvent.getType().isBegin()) {
				if (txnDepthMap.get(t) == 0L) {
					processEvent(handlerEvent);
				}
				txnDepthMap.put(t, txnDepthMap.get(t) + 1L);
			}

			else if (handlerEvent.getType().isEnd()) {
				if (txnDepthMap.get(t) == 1L) {
					processEvent(handlerEvent);
				}
				txnDepthMap.put(t, txnDepthMap.get(t) - 1L);

				if (txnDepthMap.get(t) < 0L) {
					System.err.println("Ill-formed trace: end event does not have a matching begin event");
				}
			}

			else {
				if (skipEvent(handlerEvent)) {
					continue;
				}

				// Unary transaction
				if (txnDepthMap.get(t) == 0L) {
					Event beginTxnEvent = new Event();
					beginTxnEvent.updateEvent(handlerEvent.getAuxId(), handlerEvent.getLocId(), handlerEvent.getName(), EventType.BEGIN, handlerEvent.getThread());
					processEvent(beginTxnEvent);

					processEvent(handlerEvent);

					Event endTxnEvent = new Event();
					endTxnEvent.updateEvent(handlerEvent.getAuxId(), handlerEvent.getLocId(), handlerEvent.getName(), EventType.END, handlerEvent.getThread());
					processEvent(endTxnEvent);
				}

				else {
					processEvent(handlerEvent);
				}
			}
		}
	}

	public void analyzeTraceRR() {
		while(rrParser.checkAndGetNext(handlerEvent)){
			if(! skipEvent(handlerEvent)){
				processEvent(handlerEvent);
			}
		}
	}

	private  boolean skipEvent(Event handlerEvent){
		boolean skip = false;
		if(this.removeThreadLocalEvents){
			if(!threadSet.contains(handlerEvent.getThread().getName())){
				skip = true;
			}
			if(handlerEvent.getType().isExtremeType()){
				if(!threadSet.contains(handlerEvent.getTarget().getName())){
					skip = true;
				}
			}
			if(handlerEvent.getType().isAccessType()){
				if(!variablesWritten.contains(handlerEvent.getVariable().getName())){
					skip = true;
					//					System.out.println("Read only " + handlerEvent.getVariable().getName());
					//					System.out.println("Threads " + variableToThreadSet.get(handlerEvent.getVariable().getName()));
				}
				if(variableToThreadSet.get(handlerEvent.getVariable().getName()).size() <= 1 ){
					skip = true;
				}
			}
			if(handlerEvent.getType().isLockType()){
				if(lockToThreadSet.get(handlerEvent.getLock().getName()).size() <= 1 ){
					skip = true;
				}
			}
		}
		return skip;	
	}

	private void dumpMap(){
		String filename = this.path_to_map_file;
//		if(this.removeThreadLocalEvents){
//			filename = filename + ".shared.txt";
//		}
//		else{
//			filename = filename + ".all.txt";
//		}
		try{
			FileWriter fw = new FileWriter(filename);
			for(int eidx = 0; eidx < event_index; eidx ++ ){
				fw.write(Integer.toString(eidx) + "|" + eventStrings.get(eidx) + "\n");
			}
			fw.close();
		}
		catch(IOException e){
			e.printStackTrace();
		}
	}

	private void processEvent(Event handlerEvent) {
		//		System.out.println(handlerEvent.toString());

		Thread t = handlerEvent.getThread();
		Long this_index = 0L;

		if (handlerEvent.getType().isRead()) {
			Variable v = handlerEvent.getVariable();
			if (readMap.containsKey(t)) {
				if (readMap.get(t).containsKey(v)) {
					this_index = readMap.get(t).get(v);
				} else {
					this_index = event_index;
					readMap.get(t).put(v, this_index);
					eventStrings.add(handlerEvent.toCompactString());
					event_index = event_index + 1;
				}
			} else {
				readMap.put(t, new HashMap<Variable, Long> ());
				this_index = event_index;
				readMap.get(t).put(v, this_index);
				eventStrings.add(handlerEvent.toCompactString());
				event_index = event_index + 1;
			}
		}

		else if (handlerEvent.getType().isWrite()) {
			Variable v = handlerEvent.getVariable();
			if (writeMap.containsKey(t)) {
				if (writeMap.get(t).containsKey(v)) {
					this_index = writeMap.get(t).get(v);
				} else {
					this_index = event_index;
					writeMap.get(t).put(v, this_index);
					eventStrings.add(handlerEvent.toCompactString());
					event_index = event_index + 1;
				}
			} else {
				writeMap.put(t, new HashMap<Variable, Long> ());
				this_index = event_index;
				writeMap.get(t).put(v, this_index);
				eventStrings.add(handlerEvent.toCompactString());
				event_index = event_index + 1;
			}
		}

		else if (handlerEvent.getType().isAcquire()) {
			Lock l = handlerEvent.getLock();
			if (acquireMap.containsKey(t)) {
				if (acquireMap.get(t).containsKey(l)) {
					this_index = acquireMap.get(t).get(l);
				} else {
					this_index = event_index;
					acquireMap.get(t).put(l, this_index);
					eventStrings.add(handlerEvent.toCompactString());
					event_index = event_index + 1;
				}
			} else {
				acquireMap.put(t, new HashMap<Lock, Long> ());
				this_index = event_index;
				acquireMap.get(t).put(l, this_index);
				eventStrings.add(handlerEvent.toCompactString());
				event_index = event_index + 1;
			}
		}

		else if (handlerEvent.getType().isRelease()) {
			Lock l = handlerEvent.getLock();
			if (releaseMap.containsKey(t)) {
				if (releaseMap.get(t).containsKey(l)) {
					this_index = releaseMap.get(t).get(l);
				} else {
					this_index = event_index;
					releaseMap.get(t).put(l, this_index);
					eventStrings.add(handlerEvent.toCompactString());
					event_index = event_index + 1;
				}
			} else {
				releaseMap.put(t, new HashMap<Lock, Long> ());
				this_index = event_index;
				releaseMap.get(t).put(l, this_index);
				eventStrings.add(handlerEvent.toCompactString());
				event_index = event_index + 1;
			}
		}

		else if (handlerEvent.getType().isFork()) {
			Thread tar = handlerEvent.getTarget();
			if (forkMap.containsKey(t)) {
				if (forkMap.get(t).containsKey(tar)) {
					this_index = forkMap.get(t).get(tar);
				} else {
					this_index = event_index;
					forkMap.get(t).put(tar, this_index);
					eventStrings.add(handlerEvent.toCompactString());
					event_index = event_index + 1;
				}
			} else {
				forkMap.put(t, new HashMap<Thread, Long> ());
				this_index = event_index;
				forkMap.get(t).put(tar, this_index);
				eventStrings.add(handlerEvent.toCompactString());
				event_index = event_index + 1;
			}
		}

		else if (handlerEvent.getType().isJoin()) {
			Thread tar = handlerEvent.getTarget();
			if (joinMap.containsKey(t)) {
				if (joinMap.get(t).containsKey(tar)) {
					this_index = joinMap.get(t).get(tar);
				} else {
					this_index = event_index;
					joinMap.get(t).put(tar, this_index);
					eventStrings.add(handlerEvent.toCompactString());
					event_index = event_index + 1;
				}
			} else {
				joinMap.put(t, new HashMap<Thread, Long> ());
				this_index = event_index;
				joinMap.get(t).put(tar, this_index);
				eventStrings.add(handlerEvent.toCompactString());
				event_index = event_index + 1;
			}
		}

		else if (handlerEvent.getType().isBegin()) {
			if (beginMap.containsKey(t)) {
				this_index = beginMap.get(t);
			} else {
				this_index = event_index;
				beginMap.put(t, this_index);
				eventStrings.add(handlerEvent.toCompactString());
				event_index = event_index + 1;
			}
		}

		else if (handlerEvent.getType().isEnd()) {
			if (endMap.containsKey(t)) {
				this_index = endMap.get(t);
			} else {
				this_index = event_index;
				endMap.put(t, this_index);
				eventStrings.add(handlerEvent.toCompactString());
				event_index = event_index + 1;
			}
		}

		else {
			System.err.println("Event type is illegal");
		}

		System.out.print(this_index + " ");
	}
}
