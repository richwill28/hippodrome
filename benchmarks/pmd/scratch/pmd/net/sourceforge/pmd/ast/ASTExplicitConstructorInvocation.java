/* Generated By:JJTree: Do not edit this line. ASTExplicitConstructorInvocation.java */

package net.sourceforge.pmd.ast;

public class ASTExplicitConstructorInvocation extends SimpleJavaNode {
    public ASTExplicitConstructorInvocation(int id) {
        super(id);
    }

    public ASTExplicitConstructorInvocation(JavaParser p, int id) {
        super(p, id);
    }


    /**
     * Accept the visitor. *
     */
    public Object jjtAccept(JavaParserVisitor visitor, Object data) {
        return visitor.visit(this, data);
    }

    public int getArgumentCount() {
        return ((ASTArguments) this.jjtGetChild(0)).getArgumentCount();
    }

    private String thisOrSuper;

    public void setIsThis() {
        this.thisOrSuper = "this";
    }

    public void setIsSuper() {
        this.thisOrSuper = "super";
    }

    public boolean isThis() {
        return thisOrSuper != null && thisOrSuper.equals("this");
    }

    public boolean isSuper() {
        return thisOrSuper != null && thisOrSuper.equals("super");
    }
}
