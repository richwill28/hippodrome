/* Generated By:JJTree: Do not edit this line. ASTUnparsedText.java */

package net.sourceforge.pmd.jsp.ast;

public class ASTUnparsedText extends SimpleNode {
    public ASTUnparsedText(int id) {
        super(id);
    }

    public ASTUnparsedText(JspParser p, int id) {
        super(p, id);
    }


    /**
     * Accept the visitor. *
     */
    public Object jjtAccept(JspParserVisitor visitor, Object data) {
        return visitor.visit(this, data);
    }
}
