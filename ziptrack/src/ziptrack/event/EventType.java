package ziptrack.event;

public enum EventType {
	ACQUIRE, RELEASE, READ, WRITE, FORK, JOIN, DUMMY;
	
	public boolean isAcquire(){
		return this.ordinal() == ACQUIRE.ordinal();
	}
	
	public boolean isRelease(){
		return this.ordinal() == RELEASE.ordinal();
	}
	
	public boolean isRead(){
		return this.ordinal() == READ.ordinal();
	}
	
	public boolean isWrite(){
		return this.ordinal() == WRITE.ordinal();
	}
	
	public boolean isFork(){
		return this.ordinal() == FORK.ordinal();
	}
	
	public boolean isJoin(){
		return this.ordinal() == JOIN.ordinal();
	}

	public boolean isLockType() {
		return this.isAcquire() || this.isRelease();
	}
	
	public boolean isAccessType() {
		return this.isRead() || this.isWrite();
	}

	public boolean isExtremeType() {
		return this.isFork() || this.isJoin();
	}
	
	public boolean isDummyType(){
		return this.ordinal() == DUMMY.ordinal();
	}
	
	public String toString(){
		String str = "";
		if(this.isAcquire()) str = "ACQUIRE";
		if(this.isRelease()) str = "RELEASE";
		if(this.isRead()) str = "READ";
		if(this.isWrite()) str = "WRITE";
		if(this.isFork()) str = "FORK";
		if(this.isJoin()) str = "JOIN";
		return str;
	}
}
